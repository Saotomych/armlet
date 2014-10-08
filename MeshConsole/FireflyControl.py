#!/usr/bin/python
#
# Firefly Control
#
from collections import deque
from getopt import getopt
from itertools import chain, islice
from logging import getLogger, getLoggerClass, setLoggerClass, FileHandler, Formatter, Handler, INFO, NOTSET
from os.path import basename
from sys import argv, exit # pylint: disable=W0622
from traceback import format_exc

try:
    from PyQt4 import uic
    from PyQt4.QtCore import Qt, QCoreApplication, QDateTime, QObject, QSettings, QSize, pyqtSignal
    from PyQt4.QtGui import QApplication, QDesktopWidget, QColorDialog, QDialog, QFileDialog, QMainWindow
    from PyQt4.QtGui import QHBoxLayout, QScrollArea, QStackedWidget, QWidget
    from PyQt4.QtGui import QButtonGroup, QIcon, QLabel, QLineEdit, QRadioButton, QToolButton
    from PyQt4.QtGui import QColor, QIntValidator, QSizePolicy
except ImportError, ex:
    raise ImportError("%s: %s\n\nPlease install PyQt4 v4.10.4 or later: http://riverbankcomputing.com/software/pyqt/download\n" % (ex.__class__.__name__, ex))

from UARTTextProtocol import Command, COMMAND_MARKER
from UARTTextCommands import ackResponse, ffGetCommand, ffSetCommand, ffResponse
from UARTTextCommands import UART_FF_RGB, UART_FF_WAIT, UART_FF_GOTO, UART_FF_LIGHT
from SerialPort import SerialPort, DT, TIMEOUT

LONG_DATETIME_FORMAT = 'yyyy.MM.dd hh:mm:ss'

MAIN_UI_FILE_NAME = 'FireflyControl.ui'
ABOUT_UI_FILE_NAME = 'AboutFC.ui'

LOG_FILE_NAME = 'FireflyControl.log'

FILE_FILTER = 'Firefly programs (*.ff);; All files(*)'

FILE_TEMPLATE = '''\
#
# Ostranna CG http://ostranna.ru
# Firefly program
#

%s

# End of program
'''

WINDOW_TITLE = '%s :: %s%s'

WINDOW_SIZE = 2.0 / 3
WINDOW_POSITION = (1 - WINDOW_SIZE) / 2

MAX_INT = 2 ** 31 - 1

MIN_COLOR = 0
MAX_COLOR = 255

#
# ToDo:
# - Optimize resize events with sizes updating
# - Open and Save commands
# - Hide "fade in" for first command, and "stay for" for last one (if there's no loop)
# - Set LED color immediately when color is selected in GUI
#

def fixWidgetSize(widget, adjustment = 1):
    widget.setFixedWidth(widget.fontMetrics().boundingRect(widget.text()).width() * adjustment) # This is a bad hack, but there's no better idea

def setTip(widget, tip):
    widget.setToolTip(tip)
    widget.setStatusTip(tip)

def widgets(layout, headerSize = 0, tailSize = 0): # generator
    for i in xrange(headerSize, layout.count() - tailSize):
        yield layout.itemAt(i).widget()

class CallableHandler(Handler):
    def __init__(self, emitCallable, level = NOTSET):
        Handler.__init__(self, level)
        self.emitCallable = emitCallable

    def emit(self, record):
        self.emitCallable(self.format(record))

class EventLogger(getLoggerClass(), QObject):
    logSignal = pyqtSignal(tuple, dict)

    def configure(self, parent = None):
        QObject.__init__(self, parent)
        self.logSignal.connect(self.doLog)

    def doLog(self, args, kwargs):
        super(EventLogger, self)._log(*args, **kwargs)

    def _log(self, *args, **kwargs):
        self.logSignal.emit(args, kwargs)

class VerticalScrollArea(QScrollArea):
    def __init__(self, parent = None):
        QScrollArea.__init__(self, parent)
        self.setVerticalScrollBarPolicy(Qt.ScrollBarAlwaysOn)
        self.scrollBarSet = False

    def resizeEvent(self, event):
        if not self.scrollBarSet:
            self.setVerticalScrollBarPolicy(Qt.ScrollBarAsNeeded)
            self.scrollBarSet = True
        self.setMinimumWidth(self.widget().sizeHint().width() + self.verticalScrollBar().width())
        QScrollArea.resizeEvent(self, event)

class PortLabel(QLabel):
    STATUS_COLORS = {
        SerialPort.TRYING: 'black',
        SerialPort.CONNECTED: 'brown',
        SerialPort.VERIFIED: 'green',
        SerialPort.ERROR: 'red'
    }

    setPortStatus = pyqtSignal(str, int)

    def configure(self):
        self.savedStyleSheet = str(self.styleSheet())
        self.setPortStatus.connect(self.setValue)

    def setValue(self, portName, portStatus):
        self.setText(portName)
        self.setStyleSheet(self.savedStyleSheet + '; color: %s' % self.STATUS_COLORS.get(portStatus, 'gray'))

class ConsoleEdit(QLineEdit):
    def configure(self, callback):
        self.setStatusTip(self.placeholderText())
        self.returnPressed.connect(callback)

    def getInput(self):
        ret = self.text()
        self.clear()
        return ret

class AboutDialog(QDialog):
    def __init__(self):
        QDialog.__init__(self)
        uic.loadUi(ABOUT_UI_FILE_NAME, self)

class SelectColorLabel(QLabel):
    def __init__(self, parent, callback = None, color = None):
        QLabel.__init__(self, parent)
        self.setFrameStyle(self.Box | self.Plain)
        setTip(self, "Select color")
        self.callback = callback
        self.setColor(color or QColor(Qt.white))
        self.mousePressEvent = self.editColor

    def setCorrectSize(self, size):
        size -= 2 * self.lineWidth()
        if size > 0:
            self.setFixedSize(size, size)

    def setColor(self, color):
        self.color = color
        self.setStyleSheet('background-color: %s' % color.name())
        if self.callback:
            self.callback(color)

    def editColor(self, _event):
        previousColor = self.color
        colorDialog = QColorDialog(self.color)
        colorDialog.currentColorChanged.connect(self.setColor)
        if not colorDialog.exec_():
            self.setColor(previousColor)

class TimeEdit(QLineEdit):
    MAX_VALUE = MAX_INT
    MAX_LENGTH = len(str(MAX_VALUE))

    def __init__(self, parent, callback = None, initialValue = None):
        QLineEdit.__init__(self, parent)
        self.setAlignment(Qt.AlignRight)
        self.setMaxLength(self.MAX_LENGTH)
        self.setValidator(QIntValidator(0, self.MAX_VALUE))
        self.setText(str(self.MAX_VALUE))
        fixWidgetSize(self, 1.4)
        self.setText(str(initialValue or 0))
        if callback:
            self.textEdited.connect(callback)

class DeleteButton(QToolButton):
    def __init__(self, parent, callback):
        QToolButton.__init__(self, parent)
        self.setIcon(QIcon('images/delete.png'))
        setTip(self, "Delete command")
        self.clicked.connect(callback)

    def setCorrectSize(self, size):
        self.setFixedSize(size, size)

class CommandWidget(QWidget):
    BASIC_STYLESHEET = 'border: 1px solid'
    GRADIENT_TEMPLATE = 'background: qlineargradient(x1:0, x2:1, %s)'
    GRADIENT_STOP = 'stop: %f rgb(%d, %d, %d)'

    commandsLayout = None
    correctHeight = None
    sizeDidntChange = None

    @classmethod
    def configure(cls, stackedWidget, parentWidget, updateProgramCallback):
        cls.stackedWidget = stackedWidget
        cls.parentWidget = parentWidget
        cls.updateProgramCallback = updateProgramCallback
        cls.commandsLayout = parentWidget.layout()
        cls.headerWidget = widgets(cls.commandsLayout).next()
        cls.headerLayout = cls.headerWidget.layout()
        cls.buttonGroup = QButtonGroup(parentWidget)
        cls.TAIL_SIZE = 1
        cls.HEADER_SIZE = cls.commandsLayout.count() - cls.TAIL_SIZE

    @classmethod
    def updateDeleteButtons(cls):
        hiderIndexToSet = cls.numCommands() < 2
        for command in cls.commands():
            command.hider.setCurrentIndex(hiderIndexToSet)

    @classmethod
    def updateLoop(cls, programChanged = True):
        for command in cls.commands():
            command.setLoopIcon()
        cls.updateProgram(programChanged)

    @classmethod
    def updateProgram(cls, programChanged = True):
        commands = []
        programLength = 1.0
        for (rgb, morphLength, delayLength, _command) in (c.command() for c in chain(cls.commands(), cls.loopCommands())):
            commands.append((rgb, morphLength or 1, delayLength or 1))
            programLength += (morphLength or 1) + (delayLength or 1)
        dx = programLength / 1000
        for (rgb, morphLength, delayLength) in commands:
            programLength += max(0, dx - morphLength) + max(0, dx - delayLength)
        stops = [(0, 0, 0, 0),]
        position = 0.0
        for (rgb, morphLength, delayLength) in commands:
            position += max(morphLength, dx)
            stops.append((position / programLength,) + rgb)
            position += max(delayLength, dx)
            stops.append((position / programLength,) + rgb)
        cls.updateProgramCallback(','.join(c.command()[-1] for c in cls.commands()),
                '; '.join((cls.BASIC_STYLESHEET, cls.GRADIENT_TEMPLATE % ' '.join(cls.GRADIENT_STOP % stop for stop in stops))),
                programChanged)

    @classmethod
    def setupProgram(cls, source = None):
        def parseInt(words, index, minValue = 0, maxValue = MAX_INT):
            try:
                ret = float(words[index])
                if ret % 1:
                    raise ValueError()
                ret = int(ret)
                if not minValue <= ret <= maxValue:
                    raise ValueError()
            except ValueError:
                raise ValueError("Bad program data at word %d: expected a %d..%d integer" % (index, minValue, maxValue))
            return ret

        def parseColor(words, index):
            return parseInt(words, index, MIN_COLOR, MAX_COLOR)

        words = []
        if source is not None:
            for line in source:
                words.extend(w.lower() for w in (w.strip() for w in line.strip().split('#')[0].split(',')) if w)
            if not words:
                raise ValueError("No commands found in the file")
        commands = []
        index = 0
        numCommands = 0
        gotoPosition = None
        conditionsPresent = False
        positionsToVerify = []
        while index < len(words):
            if words[index] == UART_FF_RGB:
                if len(words) < index + 5:
                    raise ValueError("Abrupt program end")
                index += 1
                r = parseColor(words, index)
                index += 1
                g = parseColor(words, index)
                index += 1
                b = parseColor(words, index)
                index += 1
                morphLength = parseInt(words, index)
                commands.append([r, g, b, morphLength, 0, numCommands])
            elif words[index] == UART_FF_WAIT:
                if len(words) < index + 2:
                    raise ValueError("Abrupt program end")
                if not commands:
                    commands.append([0, 0, 0, 0, 0, 0])
                index += 1
                commands[-1][4] += parseInt(words, index)
            elif words[index] == UART_FF_GOTO:
                if len(words) < index + 2:
                    raise ValueError("Abrupt program end")
                if conditionsPresent:
                    index += 1
                    positionsToVerify.append(index)
                else:
                    if len(words) > index + 2:
                        raise ValueError("Unexpected program data at word %d after GOTO command" % (index + 2))
                    if not commands:
                        raise ValueError("Unexpected GOTO command at the start of the program")
                    index += 1
                    gotoPosition = parseInt(words, index, 0, numCommands - 1)
            elif words[index] == UART_FF_LIGHT:
                if len(words) < index + 3:
                    raise ValueError("Abrupt program end")
                index += 1
                _light = parseInt(words, index)
                index += 1
                positionsToVerify.append(index)
                conditionsPresent = True
            numCommands += 1
            index += 1
        for index in positionsToVerify:
            parseInt(words, index, 0, numCommands - 1) # verify jump targets
        if not conditionsPresent and gotoPosition:
            for (index, command) in enumerate(commands):
                if command[-1] == gotoPosition:
                    command[-1] = True
                    break
                if command[-1] > gotoPosition:
                    raise ValueError("GOTO command targeted to a non-color command")
        cls.stackedWidget.setCurrentIndex(conditionsPresent)
        if conditionsPresent:
            cls.updateProgramCallback(','.join(words), cls.BASIC_STYLESHEET, False)
        else:
            for command in tuple(cls.commands()):
                command.delete(False)
            if commands:
                gotoCommand = None
                for (r, g, b, morphLength, delayLength, gotoHere) in commands:
                    commandWidget = CommandWidget(QColor(r, g, b), morphLength, delayLength, False)
                    if gotoHere is True:
                        gotoCommand = commandWidget
                    InsertCommandButton()
                if gotoCommand:
                    gotoCommand.radioButton.setChecked(True)
            else:
                CommandWidget(programChanged = False)
                InsertCommandButton()
            cls.updateLoop(False) # ToDo: Make this the only program update
            cls.updateDeleteButtons()

    @classmethod
    def adjustSizes(cls, size):
        if cls.sizeDidntChange < 2: # This is a hack, but I couldn't find a better way to coordinate technically independent layouts
            cls.correctHeight = size
            sampleLayout = cls.commands().next().layout()
            cls.headerLayout.setContentsMargins(sampleLayout.contentsMargins())
            widthChanged = False
            for (h, w) in zip(widgets(cls.headerLayout), widgets(sampleLayout)):
                if h.width() != w.width():
                    widthChanged = True
                    h.setFixedWidth(w.width())
            if widthChanged:
                cls.sizeDidntChange = 0
            else:
                cls.sizeDidntChange += 1
        for command in cls.commands():
            command.setCorrectSize()
        InsertCommandButton.adjustSizes(size, cls.headerWidget.height() + cls.commandsLayout.spacing())

    def __init__(self, color = None, morphLength = None, delayLength = None, index = None, programChanged = True):
        QWidget.__init__(self, self.parentWidget)
        self.setSizePolicy(QSizePolicy.Maximum, QSizePolicy.Maximum)
        self.colorLabel = SelectColorLabel(self, self.setColor, color)
        self.morphEdit = TimeEdit(self, self.updateProgram, morphLength)
        self.delayEdit = TimeEdit(self, self.updateProgram, delayLength)
        self.radioButton = QRadioButton(self)
        self.radioButton.clicked.connect(self.updateLoop)
        self.radioButton.commandWidget = self
        self.buttonGroup.addButton(self.radioButton)
        self.deleteButton = DeleteButton(self, self.delete)
        self.hider = QStackedWidget(self)
        self.hiderWidget = QWidget(self)
        hiderLayout = QHBoxLayout(self.hiderWidget)
        hiderLayout.setContentsMargins(0, 0, 0, 0)
        hiderLayout.addWidget(self.radioButton)
        hiderLayout.addWidget(self.deleteButton)
        self.hider.addWidget(self.hiderWidget)
        self.hider.addWidget(QWidget(self))
        layout = QHBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.addWidget(self.colorLabel)
        layout.addWidget(self.morphEdit)
        layout.addWidget(self.delayEdit)
        layout.addWidget(self.hider)
        for (h, w) in zip(widgets(self.headerLayout), widgets(layout)):
            if not w.toolTip() and hasattr(h, 'text'):
                setTip(w, h.text())
        self.setCorrectSize()
        lastIndex = self.commandsLayout.count() - self.TAIL_SIZE
        insertIndex = index + self.HEADER_SIZE if index != None else lastIndex
        if not self.buttonGroup.checkedButton() or insertIndex == lastIndex and not self.hasLoop():
            self.radioButton.setChecked(True)
        self.commandsLayout.insertWidget(insertIndex, self)
        self.updateLoop(programChanged)
        self.updateDeleteButtons()

    @classmethod
    def numCommands(cls):
        return cls.commandsLayout.count() - cls.HEADER_SIZE - cls.TAIL_SIZE

    @classmethod
    def commands(cls): # generator
        for w in widgets(cls.commandsLayout, cls.HEADER_SIZE, cls.TAIL_SIZE):
            yield w

    @classmethod
    def loopCommands(cls): # generator
        if not cls.hasLoop():
            return
        for w in widgets(cls.commandsLayout, cls.loopEndIndex(), cls.TAIL_SIZE):
            yield w

    @classmethod
    def loopEndIndex(cls):
        return cls.commandsLayout.indexOf(cls.buttonGroup.checkedButton().commandWidget)

    def isFirst(self):
        return self.commandsLayout.indexOf(self) == self.HEADER_SIZE

    def isLast(self):
        return self.commandsLayout.indexOf(self) == self.commandsLayout.count() - 1 - self.TAIL_SIZE

    def isLoopStart(self):
        return self.isLast()

    def isLoopEnd(self):
        return self.radioButton.isChecked()

    def isInLoop(self):
        return self.commandsLayout.indexOf(self) >= self.loopEndIndex()

    @classmethod
    def hasLoop(cls):
        return cls.buttonGroup.checkedButton() and not cls.buttonGroup.checkedButton().commandWidget.isLast()

    def setLoopIcon(self):
        self.radioButton.setIcon(QIcon('images/%s.png' % ( \
                ('noloop' if self.isLoopStart() else 'loopend') if self.isLoopEnd() else \
                'loopstart' if self.isLoopStart() else 'inloop' if self.isInLoop() else 'outofloop')))

    def resizeEvent(self, event):
        self.adjustSizes(self.setCorrectSize())
        QWidget.resizeEvent(self, event)

    def setCorrectSize(self):
        size = max(self.correctHeight, self.morphEdit.height())
        self.colorLabel.setCorrectSize(size)
        self.deleteButton.setCorrectSize(size)
        self.radioButton.setFixedWidth(size + self.radioButton.icon().actualSize(QSize(100, 100)).width())
        return size

    def setColor(self, color):
        self.color = color
        self.updateProgram()

    def delete(self, programChanged = True):
        if self.isLoopEnd():
            index = self.commandsLayout.indexOf(self)
            index = index - 1 if self.isLast() else index + 1
            if index >= self.HEADER_SIZE:
                self.commandsLayout.itemAt(index).widget().radioButton.setChecked(True)
        self.buttonGroup.removeButton(self.radioButton)
        self.setParent(None)
        self.updateLoop(programChanged)
        self.updateDeleteButtons()
        InsertCommandButton.deleteExtraButtons(self.numCommands())

    def command(self):
        fields = [UART_FF_RGB, self.color.red(), self.color.green(), self.color.blue(), self.morphEdit.text()]
        if int(self.delayEdit.text()):
            fields.extend((UART_FF_WAIT, self.delayEdit.text()))
        if self.isLoopStart() and not self.isLoopEnd():
            targetIndex = 0
            for command in islice(self.commands(), 0, self.loopEndIndex() - self.HEADER_SIZE):
                targetIndex += 1 + bool(int(command.delayEdit.text()))
            fields.extend((UART_FF_GOTO, targetIndex))
        return ((self.color.red(), self.color.green(), self.color.blue()),
                int(self.morphEdit.text()), int(self.delayEdit.text()), ','.join(str(f) for f in fields))

class InsertCommandButton(QToolButton):
    insertCommandLayout = None
    correctHeight = None

    @classmethod
    def configure(cls, parentWidget):
        cls.parentWidget = parentWidget
        cls.insertCommandLayout = parentWidget.layout()
        cls.TAIL_SIZE = 1
        cls.HEADER_SIZE = cls.insertCommandLayout.count() - cls.TAIL_SIZE

    @classmethod
    def deleteExtraButtons(cls, count):
        for button in widgets(cls.insertCommandLayout, cls.HEADER_SIZE + count + 1, cls.TAIL_SIZE):
            button.setParent(None)

    @classmethod
    def adjustSizes(cls, lineSize, headerSize):
        if lineSize > cls.correctHeight:
            cls.correctHeight = lineSize
            cls.insertCommandLayout.setContentsMargins(0, headerSize - lineSize // 2, 0, 0)
            for button in widgets(cls.insertCommandLayout, cls.HEADER_SIZE, cls.TAIL_SIZE):
                button.setCorrectSize(lineSize)

    def __init__(self):
        QToolButton.__init__(self, self.parentWidget)
        self.setIcon(QIcon('images/add.png'))
        setTip(self, "Insert command")
        self.index = self.insertCommandLayout.count() - self.TAIL_SIZE
        self.clicked.connect(self.addCommand)
        self.insertCommandLayout.insertWidget(self.index, self)
        self.setCorrectSize()

    def setCorrectSize(self, size = None):
        size = size or self.correctHeight
        if size:
            self.setFixedHeight(size)

    def addCommand(self):
        CommandWidget(index = self.index)
        InsertCommandButton()

class FireflyControl(QMainWindow):
    comConnect = pyqtSignal(str)
    comInput = pyqtSignal(str)

    def __init__(self, args):
        QMainWindow.__init__(self)
        uic.loadUi(MAIN_UI_FILE_NAME, self)
        # Processing command line options
        self.emulated = False
        self.advanced = False
        (options, _parameters) = getopt(args, 'ae', ('advanced', 'emulated'))
        for (option, _value) in options:
            if option in ('-a', '--advanced'):
                self.advanced = True
            elif option in ('-e', '--emulated'):
                self.emulated = True
        # Setting window size
        resolution = QDesktopWidget().screenGeometry()
        width = resolution.width()
        height = resolution.height()
        self.setGeometry(width * WINDOW_POSITION, height * WINDOW_POSITION, width * WINDOW_SIZE, height * WINDOW_SIZE)
        # Setting variables
        self.title = self.windowTitle()
        self.fileName = None
        self.programChanged = False
        self.currentDirectory = '.'
        self.recentFiles = deque((), 9) # ToDo: Update list and add actions to menu # pylint: disable=E1121
        # Configuring widgets
        self.portLabel.configure()
        self.resetButton.clicked.connect(self.reset)
        self.consoleEdit.configure(self.consoleEnter)
        self.newAction.triggered.connect(self.newFile)
        self.openAction.triggered.connect(self.openFile)
        self.saveAction.triggered.connect(self.saveFile)
        self.saveAsAction.triggered.connect(self.saveFileAs)
        self.aboutDialog = AboutDialog()
        self.aboutAction.triggered.connect(self.aboutDialog.exec_)
        CommandWidget.configure(self.programStackedWidget, self.commandsWidget, self.updateProgram)
        InsertCommandButton.configure(self.insertCommandWidget)
        InsertCommandButton()
        self.newFile()
        # Setup logging
        formatter = Formatter('%(asctime)s %(levelname)s\t%(message)s', '%Y-%m-%d %H:%M:%S')
        handlers = (FileHandler(LOG_FILE_NAME), CallableHandler(self.logTextEdit.appendPlainText))
        rootLogger = getLogger('')
        for handler in handlers:
            handler.setFormatter(formatter)
            rootLogger.addHandler(handler)
        rootLogger.setLevel(INFO)
        setLoggerClass(EventLogger)
        self.logger = getLogger('FireflyControl')
        self.logger.configure(self) # pylint: disable=E1103
        self.logger.info("start")
        # Starting up!
        self.loadSettings()
        # ToDo: Setup current directory and recent file name
        self.comConnect.connect(self.processConnect)
        self.comInput.connect(self.processInput)
        self.port = SerialPort(self.logger, ffGetCommand.prefix, ffResponse.prefix,
                               self.comConnect.emit, self.comInput.emit, self.portLabel.setPortStatus.emit,
                               None, (115200,))
        if self.savedMaximized:
            self.showMaximized()
        else:
            self.show()

    def newFile(self):
        # ToDo: ask if there're changes
        CommandWidget.setupProgram()
        self.fileName = None
        self.updateWindowTitle(False)

    def openFile(self):
        fileName = QFileDialog.getOpenFileName(self, "Open program", self.currentDirectory, FILE_FILTER)
        if not fileName:
            return
        fileName = unicode(fileName)
        try:
            with open(fileName) as f:
                CommandWidget.setupProgram(f)
        except:
            raise # ToDo: Do something with errors
        self.fileName = fileName
        self.updateWindowTitle(False)

    def openRecentFile(self):
        pass

    def saveFile(self, saveAs = False):
        if saveAs or not self.fileName:
            fileName = QFileDialog.getSaveFileName(self, "Save program as", self.fileName or 'New.ff', FILE_FILTER)
            if not fileName:
                return
            fileName = unicode(fileName)
        else:
            fileName = self.fileName
        try:
            with open(fileName, 'w') as f:
                f.write(FILE_TEMPLATE % self.programEdit.text())
        except:
            raise # ToDo: Do something with errors
        self.fileName = fileName
        self.updateWindowTitle(False)

    def saveFileAs(self):
        self.saveFile(True)

    def updateProgram(self, program, gradient, programChanged = True):
        if program != self.programEdit.text(): # ToDo: Wouldn't it block window title update when program contents is accidentially the same?
            self.programEdit.setText(program)
            self.graphLabel.setStyleSheet(gradient)
            self.updateWindowTitle(programChanged)

    def updateWindowTitle(self, programChanged):
        self.programChanged = self.isVisible() and programChanged
        programName = basename(self.fileName or 'New')
        dotIndex = programName.rfind('.')
        if dotIndex > 0:
            programName = programName[:dotIndex]
        self.setWindowTitle(WINDOW_TITLE % (self.title, programName, '*' if self.programChanged else ''))

    def processConnect(self, pong): # pylint: disable=W0613
        self.logger.info("connected device detected")

    def processCommand(self, source, _checked = False):
        error = True
        data = self.port.command(source.command, COMMAND_MARKER, QApplication.processEvents)
        if data:
            data = str(data).strip()
            (tag, args) = Command.decodeCommand(data)
            if tag == ackResponse.tag:
                error = args[0]
                if error:
                    self.lastCommandButton = None
                    self.logger.warning("Setup ERROR: %d", error)
                else:
                    self.lastCommandButton = source
                    self.logger.info("Setup OK")
            elif args is not None: # unexpected valid command
                self.logger.warning("Unexpected command: %s %s", tag, ' '.join(str(arg) for arg in args))
            elif tag: # unknown command
                self.logger.warning("Unknown command %s: %s", tag, data)
            else: # not a command
                self.logger.warning("Unexpected input: %s", data)
        else:
            self.logger.warning("command timed out")

    def processInput(self, data):
        error = True
        data = unicode(data).strip()
        (tag, args) = Command.decodeCommand(data)
        if tag == ackResponse.tag:
            error = args[0]
            if error:
                self.logger.warning("ERROR: %d", error)
            else:
                self.logger.info("OK")
        elif args is not None: # unexpected valid command
            self.logger.warning("Unexpected command: %s %s", tag, ' '.join(str(arg) for arg in args))
        elif tag: # unknown command
            self.logger.warning("Unknown command %s: %s", tag, data.rstrip())
        else: # not a command
            self.logger.warning("Unexpected input: %s", data.rstrip())

    def consoleEnter(self):
        data = self.consoleEdit.getInput()
        if data:
            self.lastCommandButton = None
            self.port.write(data)

    def closeEvent(self, _event):
        self.saveSettings()
        self.logger.info("close")

    def reset(self):
        self.logger.info("reset")
        self.port.reset()
        self.lastCommandButton = None

    def saveSettings(self):
        settings = QSettings()
        try:
            settings.setValue('timeStamp', QDateTime.currentDateTime().toString(LONG_DATETIME_FORMAT))
            settings.beginGroup('window')
            settings.setValue('width', self.size().width())
            settings.setValue('height', self.size().height())
            settings.setValue('x', max(0, self.pos().x()))
            settings.setValue('y', max(0, self.pos().y()))
            settings.setValue('maximized', self.isMaximized())
            settings.setValue('state', self.saveState())
            settings.endGroup()
        except:
            self.logger.exception("Error saving settings")
            settings.clear()
        settings.sync()

    def loadSettings(self):
        QCoreApplication.setOrganizationName('Ostranna')
        QCoreApplication.setOrganizationDomain('ostranna.ru')
        QCoreApplication.setApplicationName('Firefly')
        settings = QSettings()
        self.logger.info(settings.fileName())
        self.savedMaximized = False
        try:
            timeStamp = settings.value('timeStamp').toString()
            if timeStamp:
                settings.beginGroup('window')
                self.resize(settings.value('width').toInt()[0], settings.value('height').toInt()[0])
                self.move(settings.value('x').toInt()[0], settings.value('y').toInt()[0])
                self.savedMaximized = settings.value('maximized', False).toBool()
                self.restoreState(settings.value('state').toByteArray())
                settings.endGroup()
                self.logger.info("Loaded settings dated %s", timeStamp)
            else:
                self.logger.info("No settings found")
        except:
            self.logger.exception("Error loading settings")

    def error(self, message):
        print "ERROR:", message
        self.logger.error(message)
        exit(-1)

def main(args):
    try:
        application = QApplication(args)
        _fireflyControl = FireflyControl(args[1:]) # retain reference
        return application.exec_()
    except KeyboardInterrupt:
        pass
    except SystemExit:
        raise
    except BaseException:
        print format_exc()
        return -1

if __name__ == '__main__':
    main(argv)
