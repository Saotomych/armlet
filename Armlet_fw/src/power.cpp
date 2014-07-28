/*
 * power.cpp
 *
 *  Created on: 09.04.2013
 *      Author: kreyl
 */

#include "power.h"
#include "keys.h"
#include "peripheral.h"
#include "infrared.h"
#include "usb_f2.h"
#include "MassStorage.h"
#include "evt_mask.h"
#include "application.h"

#include "kl_sd.h"
#include "sd_log.h"

#define USB_ENABLED

Pwr_t Power;
static Thread *PThr;

// Table of charge
struct mVPercent_t {
    uint16_t mV;
    uint8_t Percent;
};

static const mVPercent_t mVPercentTable[] = {
        {0,     0},
        {3640, 10},
        {3670, 20},
        {3700, 30},
        {3740, 40},
        {3780, 50},
        {3800, 60},
        {3850, 70},
        {3900, 80},
        {4000, 90},
        {4100, 100},
        {4101, 100},
};
#define mVPercentTableSz    countof(mVPercentTable)
#define ADC_DEVIATION       11  // Sigma=5

// ============================== Implementation ===============================
static WORKING_AREA(waPwrThread, 128);
static void PwrThread(void *arg) {
    chRegSetThreadName("Pwr");
    Power.Task();
}

bool Pwr_t::mV2PercentHasChanged(uint16_t mV) {
    // Get pair of capacity values out of voltage
    uint8_t rTop    = mVPercentTable[mVPercentTableSz-1].Percent;
    uint8_t rBottom = mVPercentTable[mVPercentTableSz-2].Percent;
    for(uint8_t i=1; i<mVPercentTableSz-1; i++) {
        if(mV < mVPercentTable[i].mV) {
            rBottom = mVPercentTable[i-1].Percent;
            rTop    = mVPercentTable[i  ].Percent;
            break;
        }
    } // for
    // Calculate result
    if((CapacityPercent == rTop) or (CapacityPercent == rBottom)) return false; // no change
    else {
        CapacityPercent = (rTop < CapacityPercent)? rTop : rBottom;
        return true;
    }
}

__attribute__ ((__noreturn__))
void Pwr_t::Task() {
    while(true) {
        chThdSleepMilliseconds(PWR_MEASUREMENT_INTERVAL_MS);
        // Check if power src changed
        if(WasExternal and !ExternalPwrOn()) {
            WasExternal = false;
#ifdef USB_ENABLED
            Usb.Shutdown();
            MassStorage.Reset();
            chSysLock();
            Clk.SetFreq12Mhz();
            Clk.InitSysTick();
            chSysUnlock();
#endif
            Uart.Printf("\rExtPwr Off");
            if(App.PThd != nullptr) chEvtSignal(App.PThd, EVTMSK_NEW_POWER_STATE);
        }
        else if(!WasExternal and ExternalPwrOn()) {
            WasExternal = true;
#ifdef USB_ENABLED
            chSysLock();
            Clk.SetFreq48Mhz();
            Clk.InitSysTick();
            chSysUnlock();
            Usb.Init();
            chThdSleepMilliseconds(540);
            Usb.Connect();
#endif
            Uart.Printf("\rExtPwr On");
            if(App.PThd != nullptr) chEvtSignal(App.PThd, EVTMSK_NEW_POWER_STATE);
        }
        //if(IsCharging()) Uart.Printf("\rCharging");

#if 1 // ==== ADC ====
        Adc.Measure();
        uint32_t rslt = 0;
        // Calculate average
        for(uint8_t i=0; i<ADC_BUF_SZ; i++) rslt += Adc.Result[i];
        rslt /= ADC_BUF_SZ;
        // Calculate voltage
        uint32_t U = (2 * rslt * ADC_VREF_MV) / 4095;   // 2 because of resistor divider
        // Calculate percent
        if(mV2PercentHasChanged(U)) {
            // Indicate if has changed
//            Uart.Printf("\rAdc=%u; U=%u; %=%u", rslt, U, CapacityPercent);
//            Uart.Printf("\r%u", U);
            if(App.PThd != nullptr) chEvtSignal(App.PThd, EVTMSK_NEW_POWER_STATE);
        }
#endif
    } // while
}

void Pwr_t::Init() {
    // GPIO
    PinSetupIn(PWR_EXTERNAL_GPIO, PWR_EXTERNAL_PIN, pudPullDown);
    PinSetupIn(PWR_CHARGING_GPIO, PWR_CHARGING_PIN, pudPullUp);
    PinSetupAnalog(PWR_BATTERY_GPIO, PWR_BATTERY_PIN);
    Adc.Init();
    // Create and start thread
    PThr = chThdCreateStatic(waPwrThread, sizeof(waPwrThread), LOWPRIO, (tfunc_t)PwrThread, NULL);
}

//static RTCWakeup wakeupspec;

void Pwr_t::EnterStandby() {
//    // Clear flags to setup wakeup. Will not work without this.
//    RTC->ISR &= ~(RTC_ISR_ALRBF | RTC_ISR_ALRAF | RTC_ISR_WUTF | RTC_ISR_TAMP1F | RTC_ISR_TSOVF | RTC_ISR_TSF);
//    wakeupspec.wakeup = ((uint32_t)4) << 16;    // select 1 Hz clock source
//    wakeupspec.wakeup |= 9;                     // set counter value to 9. Period will be 9+1 seconds
//    rtcSetPeriodicWakeup_v2(&RTCD1, &wakeupspec);
//    //Uart.Printf("%06X, %06X, %06X, %06X\r", RTC->CR, RTC->ISR, RTC->PRER, RTC->WUTR);
//    // Enter standby
//    chSysLock();
//    SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;  // Set DEEPSLEEP bit
//    // Flash stopped in stop mode, Enter Standby mode
//    PWR->CR = PWR_CR_FPDS | PWR_CR_PDDS | PWR_CR_LPDS | PWR_CR_CSBF | PWR_CR_CWUF;
//    RTC->ISR &= ~(RTC_ISR_ALRBF | RTC_ISR_ALRAF | RTC_ISR_WUTF | RTC_ISR_TAMP1F | RTC_ISR_TSOVF | RTC_ISR_TSF);
//    __WFI();
//    chSysUnlock();
}

void ShutdownPeriphery() {
    // Shutdown periphery
    Keys.Shutdown();
    Beeper.Shutdown();
    Vibro.Shutdown();
    IR.Shutdown();
}
