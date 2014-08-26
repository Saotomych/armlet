/*
 * atlgui.cpp
 *
 *  Created on: 23 ����� 2014 �.
 *      Author: USER
 */
#include "atlgui.h"
#include"emotions.h"
#include"stddef.h"
#include"sound.h"
#include "lcd2630.h"
#include "gui.h"
#include "power.h"
#include "AtlGuiCF.h"
#include "mesh_lvl.h"
#include "application.h"
AtlGui_t AtlGui;
#define PATH_FOLDER_STR "\\"
#define PATH_TO_GUI "\\GUI\\"
#define GUI_PATH_EXT ".bmp"
char bmp_filename[128];
void AtlGui_t::Init()
{
    is_splash_screen_onrun=0;
    on_run=0;
    current_state=-1;
    is_gui_shown=false;
    is_locked=false;
    is_lock_now=false;
    is_screen_suspended=false;
    is_suspend_timer_run=false;
    is_lock_redraw_active=false;
    //init gui pos andfunc arrays

    //get screen ptrs
    char * ptr_main=NULL;
    char *ptr_intentions=NULL;
    for(int i=0;i<screens_number;i++)
    {
        if(strcmp("main",screens[i].name)==0)
            ptr_main=(char*)screens[i].name;
        if(strcmp("intentions",screens[i].name)==0)
            ptr_intentions=(char*)screens[i].name;
    }
    //put screen ptr
    for(int i=0;i<screens_number;i++)
    {
        if(strcmp("main",screens[i].name)==0)
           screens[i].screen_switch[7]=ptr_intentions;
        if(strcmp("intentions",screens[i].name)==0)
            screens[i].screen_switch[7]=ptr_main;
    }
    //init charname
    GetCharname();
    //init funcs
    for(int i=0;i<screens_number;i++)
    {
        if(strcmp("main",screens[i].name)==0)
        {
            //���������� ������
            //new track
            screens[i].buttons[3].isPressable=bChangeMelodyCheck;
            screens[i].buttons[3].press=bChangeMelody;
            //sound up
            screens[i].buttons[4].isPressable=bSoundUpCheck;
            screens[i].buttons[4].press=bSoundUpChange;
            //sound down
            screens[i].buttons[5].isPressable=bSoundDownCheck;
            screens[i].buttons[5].press=bSoundDownChange;
            //lock
            screens[i].buttons[6].isPressable=bLockCheck;
            screens[i].buttons[6].press=bLockChange;

            //sex
            screens[i].buttons[2].isPressable=bReasonCheck;
            screens[i].buttons[2].getState=bReasonGetState;
            screens[i].buttons[2].press=bReasonChange;
            //fight
            screens[i].buttons[1].isPressable=bReasonCheck;
            screens[i].buttons[1].getState=bReasonGetState;
            screens[i].buttons[1].press=bReasonChange;
        }
        if(strcmp("intentions",screens[i].name)==0)
        {
            //kill
            screens[i].buttons[1].isPressable=bReasonCheck;
            screens[i].buttons[1].getState=bReasonGetState;
            screens[i].buttons[1].press=bReasonChange;
            //create
            screens[i].buttons[3].isPressable=bReasonCheck;
            screens[i].buttons[3].getState=bReasonGetState;
            screens[i].buttons[3].press=bReasonChange;


            //destroy
            screens[i].buttons[2].isPressable=bReasonCheck;
            screens[i].buttons[2].getState=bReasonGetState;
            screens[i].buttons[2].press=bReasonChange;

            //death
            screens[i].buttons[5].isPressable=bReasonCheck;
            screens[i].buttons[5].getState=bReasonGetState;
            screens[i].buttons[5].press=bReasonChange;


            //lock
            screens[i].buttons[6].isPressable=bLockCheck;
            screens[i].buttons[6].press=bLockChange;

        }

    }
}
void AtlGui_t::TurnOnScreen()
{
    if(!is_screen_suspended)
    {
        Uart.Printf("\rAtlGui_t::TurnOffScreen already turned on");
        return;
    }
    is_screen_suspended=false;
    Uart.Printf("\rAtlGui_t::TurnOffScreen  turn on");
    Lcd.SetBrightness(100);
    screen_suspend_timer=0;
    RenderFullScreen(current_state);
}
void AtlGui_t::TurnOffScreen()
{
    if(is_screen_suspended)
    {
        Uart.Printf("\rAtlGui_t::TurnOffScreen already turned off");
        return;
    }
    is_screen_suspended=true;
    this->current_state=0;//TODO set onmainwith search main screen
    is_locked=true;
    Uart.Printf("\rAtlGui_t::TurnOffScreen  turn off");
    Lcd.SetBrightness(0);
}
void AtlGui_t::AddSuspendScreenTimer(int sec_to_add)
{
    if(is_screen_suspended)
        return;
    if(is_suspend_timer_run)
    {
        this->screen_suspend_timer=0;
        return;
    }
    screen_suspend_timer+=sec_to_add;
   // Uart.Printf("AtlGui_t::AddSuspendScreenTimer  screen_suspend_timer %d \r",screen_suspend_timer);
    if(screen_suspend_timer>=SUSPEND_SCREEN_SEC)
        TurnOffScreen();
};
void AtlGui_t::ReactLockedPress()
{
    Uart.Printf("\rCALL ON LOCKED SCREEN");
}
void AtlGui_t::ShowSplashscreen()
{
//TODO summ path to gui and splash
    is_splash_screen_onrun=1;
#ifdef TEXT_OR_BMP_SCREEN
    Lcd.Printf(11, 11, clGreen, clBlack, "SPLASH SCREEN");
#else
   Lcd.DrawBmpFile(0,0,"splash.bmp");
#endif
   Sound.Play("splash.mp3");
}
void AtlGui_t::CallStateScreen(int screen_id)
{
    Uart.Printf("\r%S", __func__);
   // Lcd.Printf(11, 21, clGreen, clBlack, "STATESCREEN %u", screen_id);
   if(current_state==screen_id)
   {
       //���� ��������� �� ����� �����, �� �������������� ���
       //Lcd.Printf(11, 31, clGreen, clBlack, "C stscr %u same", screen_id);
       return;
   }
   current_state=screen_id;
   RenderFullScreen(current_state);
  // Lcd.Printf(11, 31, clGreen, clBlack, "C stscr %u new", screen_id);
   //TODO render??
}
void AtlGui_t::RenderFullScreen(int screen_id)
{
    Uart.Printf("\r%S", __func__);
    //get filename
    strcpy(bmp_filename,PATH_TO_GUI);
    strcat(bmp_filename,screens[screen_id].name);
    strcat(bmp_filename,PATH_FOLDER_STR);
    strcat(bmp_filename,"back");
    strcat(bmp_filename,GUI_PATH_EXT);
    Uart.Printf("\rRenderFullScreen %s", bmp_filename);
    // render it
    Lcd.DrawBmpFile(0,0,bmp_filename);
    RenderNameTimeBat();
    //and all buttons
    for(int i=0;i<9;i++)
    {
        if(screens[screen_id].buttons[i].isPressable!=nullptr)
        {
            int state1=screens[screen_id].buttons[i].isPressable(screen_id,i);
            int state2=-1;
            if(screens[screen_id].buttons[i].getState!=nullptr)
                state2= screens[screen_id].buttons[i].getState(screen_id,i);
            //if(state2!=BUTTON_NORMAL || state2!=-1 ||state2!=BUTTON_PRESSABLE)
            if(state2==BUTTON_ENABLED)
                RenderSingleButton(screen_id,i,state2);
            else
           // Uart.Printf("\r RenderFullScreen state2 %d, indx %d",state2,i);
            RenderSingleButton(screen_id,i,state1);
        }//�� �������� ������, ������� ���
    }
    // Draw battery
    Lcd.DrawBatteryState();
}
void AtlGui_t::ButtonIsReleased(int button_id ,KeyEvt_t Type)
{

    int kmode=-1;
    if(Type==keRelease)
        kmode=0;
    if(Type==keLongPress)
        kmode=2;
    if(Type==keRepeat)
        kmode=3;

    is_suspend_timer_run=false;
    if(is_screen_suspended)
    {
        this->TurnOnScreen();
        return;
    }
    if(is_locked && button_id!=6)//���� �� ��� � �������� - ���������
        return;

    if(current_state>=0 && current_state<screens_number)
    {
        if( screens[current_state].buttons[button_id].isPressable!= nullptr)
        {
            int button_state_val=screens[current_state].buttons[button_id].isPressable(current_state,button_id);//sptr_button_state[button_id]->fptr_on_press());
            Uart.Printf("\rbutton_state_val %d",button_state_val);
            if(button_state_val==BUTTON_PRESSABLE)
            {
                //�������� ������� ������, ���� ����
                //���� ������ �������,� ����� ���-�� �������,�������� ������ ������
                if( screens[current_state].buttons[button_id].press!= nullptr)
                {
                   int new_b_state= screens[current_state].buttons[button_id].press(current_state,button_id,kmode);
                   if(new_b_state!=BUTTON_NO_REDRAW)
                       //return mb???
                   RenderSingleButton(current_state,button_id,new_b_state);
                   else
                       return; // ������ ������� ����� ����� �������,������� �������, ��� ����� ����� �� ��������
                }
                else
                {
                    Uart.Printf("\rButtonIsClicked render func absent");
                }
                //�������� ����� �����,���� ����
                Uart.Printf("\rCallStateScreen %d",screens[current_state].screen_switch[button_id]);
                  if( screens[current_state].screen_switch[button_id]!=nullptr  )
                  {
                      Uart.Printf("\r!!!!CallStateScreen %s",screens[current_state].screen_switch[button_id]);
                      // �������� ����� ��������� ������, ������������
                      int new_scrid=AtlGui.GetScreenIndxFromName((char*)screens[current_state].screen_switch[button_id]);
                      if(new_scrid>=0)
                          CallStateScreen(new_scrid);
                      else
                      {
                          Uart.Printf("\rERROR in AtlGui_t::ButtonIsReleased on GetScreenIndxFromName");
                          return;
                      }
                      //�������� ������� ������ ����� �����
                      if(screens[current_state].fptr_gui_state_array_switch_functions[button_id]!= NULL)
                          screens[current_state].fptr_gui_state_array_switch_functions[button_id]();
                  }

            }
        }
        else return;// ������ ������� ����� ����� �������, ������� ���! ���� ������ ���������
     }
}
int AtlGui_t::GetScreenIndxFromName(char* name)
{
    for(int i=0;i<screens_number;i++)
        if(strcmp(name,screens[i].name)==0)
            return i;
    return -1;
}
void AtlGui_t::RenderSingleButtonStateCheck(int screen_id,int button_id)
{
    if(current_state>=0 && current_state<screens_number)
           if( screens[current_state].buttons[button_id].isPressable!= nullptr)
           {
              int button_state_val=screens[current_state].buttons[button_id].isPressable(current_state,button_id);//sptr_button_state[button_id]->fptr_on_press());
              Uart.Printf("\r RenderSingleButtonStateCheck button_state_val3 %d",button_state_val);
              if(button_state_val==BUTTON_PRESSABLE)
              {
                  Uart.Printf("\r RenderSingleButtonStateCheck button%d on screen %d is pressable %d",button_id,current_state );
                  RenderSingleButton(current_state,button_id,BUTTON_PRESSED);
                  //on red andgreenchange state
                  return;
              }
              else
                  RenderSingleButton(current_state,button_id,BUTTON_DISABLED);
           }
}
bool AtlGui_t::ButtonIsClicked(int button_id)
{
    if(is_screen_suspended)
        return false;
    is_suspend_timer_run=true;

    if(is_locked && button_id!=6)
        return false;
    //�������� ������ ������
    Uart.Printf("\rcurrent_state %d screens_number %d button id %d",current_state,screens_number,button_id);
    if(current_state>=0 && current_state<screens_number)
    {
        if( screens[current_state].buttons[button_id].isPressable!= nullptr)
        {
           int button_state_val=screens[current_state].buttons[button_id].isPressable(current_state,button_id);//sptr_button_state[button_id]->fptr_on_press());
           Uart.Printf("\rbutton_state_val3 %d",button_state_val);
           if(button_state_val==BUTTON_PRESSABLE)
           {
               Uart.Printf("\rbutton%d on screen %d is pressable %d",button_id,current_state );
               RenderSingleButton(current_state,button_id,BUTTON_PRESSED);
               //on red andgreenchange state
               return true;
           }
           else {
               RenderSingleButton(current_state,button_id,BUTTON_DISABLED);
               return true;
           }
        }
        else return false;
      // else return; // ���� ��� �������� �� ������ - ������ �� ������

    }
    else return false; // ���� �� ��� �������� ��������� - ������!


    // ���� ���� ��������� ������, ������ �����??

    //������������ ���������??

//
//    //�������� ������ ������
//    if(current_state>=0 && current_state<screens_number)
//    {
//       if( gui_states[current_state].sptr_button_state[button_id]->fptr_on_press()!=NULL)
//       {
//           int button_state_val=*(gui_states[current_state].sptr_button_state[button_id]->fptr_on_press());
//           if(button_state_val>0)
//           {
//               //on red andgreenchange state
//               //���� ������ �������,� ����� ���-�� �������,�������� ������ ������
//               gui_states[current_state].sptr_button_state[button_id]->fptr_change_state_id();
//
//           }
//       }
//       //�������� ����� �����,���� ����
//       if( gui_states[current_state].gui_state_array_switch[button_id]>=0)
//       {
//           if(gui_states[current_state].fptr_gui_state_array_switch_functions[button_id]!= NULL)
//               gui_states[current_state].fptr_gui_state_array_switch_functions[button_id]();
//       }
//
//
//
//      // else return; // ���� ��� �������� �� ������ - ������ �� ������
//
//    }
//    else return; // ���� �� ��� �������� ��������� - ������!
//
//
//    // ���� ���� ��������� ������, ������ �����??
//
//    //������������ ���������??
}
void AtlGui_t::RenderNameTimeBat()
{
    //timechar
    if(Mesh.GetAstronomicTime(timechar,5)==FAILURE)
        strcpy(timechar,"TM.FL");

    Lcd.Printf(B52,1, 0, clAtltopstr, clAtlBack, "%s",char_name);
    Lcd.Printf(B52,96-2, 0, clAtltopstr, clAtlBack, " %s",timechar);

}
void AtlGui_t::GetCharname()
{
    //ID
    int chsize=sizeof(reasons[App.ID])/sizeof(char);
    if(chsize>MAX_CHARNAME_LCD_SIZE)
        chsize=MAX_CHARNAME_LCD_SIZE;
    strncpy(char_name,reasons[App.ID].name,chsize);
}
void AtlGui_t::DrawBigLockMark()
{
    Lcd.DrawBmpFile(LOCK_X,LOCK_Y,"/GUI/lock.bmp");
}
void AtlGui_t::DrawSondLvlMark()
{
    Lcd.DrawBmpFile(155,60,"/GUI/main/volume.bmp");
    Lcd.DrawBmpFile(157,125 - (6 * current_volume_lvl/25),"GUI/main/mark.bmp");
}
void AtlGui_t::RenderSingleButton(int screen_id,int button_id,int button_state)
{
    //getfilename
    //get filename
    //absent 4
    //disabled
    //enabled
    //normal 1
    //not
    //pressed

//#define BUTTON_NOT_PRESSED 0
//#define BUTTON_NORMAL 1
//#define BUTTON_ENABLED 2
//#define BUTTON_DISABLED 3
//#define BUTTON_ABSENT 4
//#define BUTTON_PRESSED 5
//#define BUTTON_ERROR 6
        strcpy(bmp_filename,PATH_TO_GUI);
        strcat(bmp_filename,screens[screen_id].name);
        strcat(bmp_filename,PATH_FOLDER_STR);
        //strcat(bmp_filename,"back");
        //state switcher, takes data from defines
        if(button_state==BUTTON_ABSENT)
            strcat(bmp_filename,"absent");
        else if(button_state==BUTTON_DISABLED)
            strcat(bmp_filename,"disabled");
        else if(button_state==BUTTON_ENABLED)
            strcat(bmp_filename,"enabled");
        else if(button_state==BUTTON_NORMAL)
            strcat(bmp_filename,"normal");
        else if(button_state==BUTTON_NOT_PRESSED)
            strcat(bmp_filename,"not");
        else if(button_state==BUTTON_PRESSED)
            strcat(bmp_filename,"pressed");

        strcat(bmp_filename,PATH_FOLDER_STR);
       // strcat(bmp_filename,buttons_arr.[button_id]);
        //��������!
       // char bstr[sizeof(BUTTONS)];
        //char bstr1[1];
        //memcpy(bstr,BUTTONS,sizeof(BUTTONS));
        //memcpy(bstr1,&bstr+button_id,1);
        //�������� �����
        //�� ����� (
        //strcat(bmp_filename,bstr1);

        //��������
        //#define BUTTONS "ABCLERXYZ"
        if(button_id==0)
            strcat(bmp_filename,"A");
        else if(button_id==1)
            strcat(bmp_filename,"B");
        else if(button_id==2)
            strcat(bmp_filename,"C");
        else if(button_id==3)
            strcat(bmp_filename,"X");
        else if(button_id==4)
            strcat(bmp_filename,"Y");
        else if(button_id==5)
            strcat(bmp_filename,"Z");
        else if(button_id==6)
            strcat(bmp_filename,"L");
        else if(button_id==7)
            strcat(bmp_filename,"E");
        else if(button_id==8)
            strcat(bmp_filename,"R");
        //�������� �����

      // strchr(BUTTONS,'B');
        strcat(bmp_filename,GUI_PATH_EXT);
//        Uart.Printf("\rRenderSingleButton %s left %d Bot %d",bmp_filename,screens[screen_id].buttons[button_id].left,screens[screen_id].buttons[button_id].bottom);
        // render it
        Lcd.DrawBmpFile(screens[screen_id].buttons[button_id].left,screens[screen_id].buttons[button_id].bottom,bmp_filename);

        //is_lock_redraw_active

        if(screens[screen_id].buttons[button_id].press==bLockChange)
        {
            if(is_locked==true)
            DrawBigLockMark();
            if(is_lock_now==true)
            {
                is_lock_now=false;
                RenderFullScreen(screen_id);
               // is_lock_redraw_active=false;
            };

        }

        if(screens[screen_id].buttons[button_id].press==bSoundUpChange || screens[screen_id].buttons[button_id].press==bSoundDownChange)
        {
            DrawSondLvlMark();
            //

        }
//        if(screens[screen_id].buttons[button_id].press==bSoundUpChange || screens[screen_id].buttons[button_id].press==bSoundDownChange)
//        {
//            DrawSondLvlMark();
//           // RenderFullScreen(screen_id);
//
//        }
}
