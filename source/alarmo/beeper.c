#include <alarmo/beeper.h>

#include <math.h>
#include <string.h>

#include <alarmo/mcuHwc.h>
#include <alarmo/rgbl.h>
#include <alarmo/settings.h>
#include <alarmo/state.h>
#include <alarmo/utils.h>

#define BEEP1_STATE 20 // 2 beeps/s
#define BEEP2_STATE 40 // 4 beeps/s
#define BEEP3_STATE 60 // 10 beeps/s

#define BEEP_SAMPLERATE 32000
#define BEEP_BYTESPERSAMPLE 4

/*

/// PDN wake events and MCU interrupts to select, combined with those of other processes
typedef struct PtmWakeEvents {
	u32 pdn_wake_events;    ///< Written to PDN_WAKE_EVENTS. Don't select bit26 (MCU), PTM will do it automatically.
	u32 mcu_interupt_mask;  ///< MCU interrupts to check when a MCU wake event happens.
} PtmWakeEvents;

typedef struct {
	PtmWakeEvents exit_sleep_events;       ///< Wake events for which the system should fully wake up.
	PtmWakeEvents continue_sleep_events;   ///< Wake events for which the system should return to sleep.
} PtmSleepConfig;

*/

bool beepEmit(u16 freq, u16 ms, bool force) {
    if (!(alarmoSettings & ASET_BEEP) && !force) {
        srwait(ms,)
        return !alarmoRinging;
    }

    const u32 totalSamples = (u32)(BEEP_SAMPLERATE / (float)(1000/ms) / 2);

    ndspWaveBuf waveBuf[2];
	u32 *audioBuffer = (u32*)linearAlloc(totalSamples*BEEP_BYTESPERSAMPLE*2);

	ndspSetOutputMode(NDSP_OUTPUT_STEREO);

    ndspChnReset(0);
	ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
	ndspChnSetRate(0, BEEP_SAMPLERATE);
	ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);

	float mix[12];
	memset(mix, 0, sizeof(mix));
	mix[0] = 1.0;
	mix[1] = 1.0;
	ndspChnSetMix(0, mix);

	memset(waveBuf,0,sizeof(waveBuf));
	waveBuf[0].data_vaddr = &audioBuffer[0];
	waveBuf[0].nsamples = totalSamples;
	waveBuf[1].data_vaddr = &audioBuffer[totalSamples];
	waveBuf[1].nsamples = totalSamples;

    for (int i=0; i<totalSamples*2; i++) {
        s16 sample = INT16_MAX * sin(freq*(2*M_PI)*i/BEEP_SAMPLERATE);
        audioBuffer[i] = (sample<<16) | (sample & 0xffff);
    }
    DSP_FlushDataCache(audioBuffer, totalSamples * 2);

	ndspChnWaveBufAdd(0, &waveBuf[0]);
	ndspChnWaveBufAdd(0, &waveBuf[1]);

    /*while (!((waveBuf[0].status == NDSP_WBUF_DONE && waveBuf[1].status == NDSP_WBUF_DONE) || !alarmoRinging || !force))
	    svcSleepThread(1000000);*/
    svcSleepThread(1250000*ms);
    
    linearFree(audioBuffer);

    return alarmoStop;
}
bool beepCb() {
    alarmoRinging++;
    if (alarmoSettings & ASET_PWLB) {
        mcuHwcInit();
        u8 mcuPWB = 0xFF;
        MCUHWC_WriteRegister(0x28, &mcuPWB, 1);
        mcuHwcExit();
    }
    if (alarmoSettings & ASET_PRLF) {
        u8 amount = 1;
        if (alarmoRinging >= BEEP1_STATE+1) {
            if (alarmoRinging >= BEEP2_STATE+1)
                if (alarmoRinging >= BEEP3_STATE+1)
                    amount = 9;
                else amount = 4;
            else amount = 2;
        }
        rgbl_changeLed(alarmoRgbBeep0);
        if (!beepEmit(2093 * 2, 50, false))
        for (u8 i = 0; i < amount-1 && alarmoRinging; i++) {
            //if (amount == 255) i = 0;
            srwait(50, )
            rgbl_changeLed(alarmoRgbBeep0);
            if (beepEmit(2093 * 2, 50, false)) break;
        }
    } else {
        rgbl_changeLed(alarmoRgbBeep0);
        if (!beepEmit(2093 * 2, 50, false))
        for (u8 i = 0; i < 3 && alarmoRinging; i++) {
            srwait(50, )
            rgbl_changeLed(alarmoRgbBeep0);
            if (beepEmit(2093 * 2, 50, false)) break;
        }
    }
    if (alarmoSettings & ASET_PWLB) {
        mcuHwcInit();
        u8 mcuPWB = 0x00;
        MCUHWC_WriteRegister(0x28, &mcuPWB, 1);
        mcuHwcExit();
    }

    return alarmoStop;
}
void beepEndCb() {
    if (alarmoRepeatAt != 0) alarmoRepeatAt = 0;

    if (alarmoSettings & ASET_PWLB) {
        mcuHwcInit();
        u8 mcuPWB = 0xFF;
        MCUHWC_WriteRegister(0x28, &mcuPWB, 1);
        mcuHwcExit();
    }
    alarmoTinyNotify("Stopped.", 3);

    rgbl_changeLed(alarmoRgbEnd);
    beepEmit(1661, 100, true);
    beepEmit(2093, 100, true);
    beepEmit(2489, 100, true);
    svcSleepThread(100000000);

    alarmoRinging = 0;
    if (alarmoShut) {
        alarmoShut = false;
    }

    //alarmoRingTime[1] = (alarmoCurTime->tm_min+1) % 60;
    //alarmoRingTime[0] = alarmoRingTime[1]==0 ? alarmoCurTime->tm_hour+1 : alarmoCurTime->tm_hour;
}