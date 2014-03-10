/*
 * peripheral.cpp
 *
 *  Created on: 18.01.2013
 *      Author: kreyl
 */

#include "ch.h"
#include "hal.h"
#include "clocking.h"
#include "string.h"     // For memcpy
#include "peripheral.h"

// Variables
//PwrStatus_t PwrStatus;

// ================================== Beep =====================================
#define BEEP_TOP_VALUE   22
Beeper_t Beeper;
// Timer callback
void BeeperTmrCallback(void *p) {
    chSysLockFromIsr();
    Beeper.BeepI((const BeepChunk_t*)p);
    chSysUnlockFromIsr();
}

void Beeper_t::Init() {
    IPin.Init(GPIOD, 12, TIM4, 1, BEEP_TOP_VALUE);
}

void Beeper_t::BeepI(const BeepChunk_t *PSequence) {
    // Reset timer
    if(chVTIsArmedI(&ITmr)) chVTResetI(&ITmr);
    if(PSequence == nullptr) {
        IPin.Off();
        return;
    }
    // Set sound
    IPin.SetFreqHz(PSequence->Freq_Hz);
    IPin.Set(PSequence->Volume);
    // Proceed sequence, stop it or restart
    const BeepChunk_t *PCh = nullptr;
    switch(PSequence->ChunkKind) {
        case ckNormal: PCh = PSequence + 1; break;
        case ckStop:                        break;
        case ckRepeat: PCh = IPFirstChunk;  break;
    }
    // Start timer
    chVTSetI(&ITmr, MS2ST(PSequence->Time_ms), BeeperTmrCallback, (void*)PCh);
}

void Beeper_t::Shutdown() {
    PinSetupAnalog(GPIOD, 12);
}

// ================================== Vibro ====================================
#define VIBRO_TOP_VALUE   207
Vibrator_t Vibro;
// Timer callback
void VibroTmrCallback(void *p) {
    Vibro.VibrateI((const VibroChunk_t*)p);
}

void Vibrator_t::Init() {
    IPin.Init(GPIOE, 14, TIM1, 4, VIBRO_TOP_VALUE);
    IPin.SetFreqHz(171);
}

void Vibrator_t::VibrateI(const VibroChunk_t *PSequence) {
    // Reset timer
    if(chVTIsArmedI(&ITmr)) chVTResetI(&ITmr);
    // Process chunk
    int8_t Intencity = PSequence->Intencity;
    if((Intencity < 0) or (PSequence->Time_ms == 0)) {    // Nothing to play
        IPin.Off();
        return;
    }
    if(Intencity > 100) Intencity = 100;  // Normalize volume
    IPin.Set(Intencity);
    // Start timer
    chVTSetI(&ITmr, MS2ST(PSequence->Time_ms), VibroTmrCallback, (void*)(PSequence+1));
}

void VibroTmrCallbackStop(void *p) {
    Vibro.IPin.Off();
}
void Vibrator_t::Vibrate(uint32_t ms) {
    chVTReset(&ITmr);
    IPin.Set(100);
    chVTSet(&ITmr, MS2ST(ms), VibroTmrCallbackStop, NULL);
}

void Vibrator_t::Shutdown() {
    PinSetupAnalog(GPIOE, 14);
}

// ================================= Pin control ===============================
Pin_t Pin;
void PinTmrCallback(void *p) {
    Pin.Low();
}

void Pin_t::Pulse(uint32_t ms) {
    chVTReset(&ITmr);
    High();
    // Start timer to switch off
    chVTSet(&ITmr, MS2ST(ms), PinTmrCallback, NULL);
}
