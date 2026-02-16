#include <alarmo/routine.h>

#include <alarmo/beeper.h>
#include <alarmo/citro.h>
#include <alarmo/rgbl.h>
#include <alarmo/settings.h>
#include <alarmo/state.h>
#include <alarmo/utils.h>

Handle alarmoMutex;

void alarmoMainInit() {
    consoleClear();
    printf(
        CONSOLE_ESC(37m)
        "SELECT      Settings\n"
        "D-PAD UP    Increase brightness\n"
        "D-PAD DOWN  Decrease brightness\n"
        "%s       Quit\n\n"
        "When alarm rings, press\nany button to shut it.\nBeep sound is muted while sleep mode."
        CONSOLE_RESET,
        envIsHomebrew() ? "START" : "HOME "
    );

    gfxScreenSwapBuffers(GFX_BOTTOM, false);
    alarmoState = true;

    C2D_TextBufDelete(hourBuf);
    hourBuf = C2D_TextBufNew(6);
    C2D_TextBufDelete(hourSecBuf);
    hourSecBuf = C2D_TextBufNew(3);
    C2D_TextBufDelete(alarmBuf);
    alarmBuf = C2D_TextBufNew(6);

    C2D_TextBufDelete(apTopBuf[0]);
    C2D_TextBufDelete(apTopBuf[1]);
    apTopBuf[0] = C2D_TextBufNew(3);
    apTopBuf[1] = C2D_TextBufNew(3);

    C2D_TextBufDelete(apBotBuf[0]);
    C2D_TextBufDelete(apBotBuf[1]);
    apBotBuf[0] = C2D_TextBufNew(3);
    apBotBuf[1] = C2D_TextBufNew(3);

    C2D_TextBufDelete(botBuf);
    botBuf = C2D_TextBufNew(21);

    if (alarmoSettings & ASET_POBS) {
        gspLcdInit();
        GSPLCD_PowerOffBacklight(GSPLCD_SCREEN_BOTTOM);
        gspLcdExit();
    }
}

void mainPara(void* p) {
    u8 lastsec = 0;

    // Avoids audio to be only emitted to JACK while sleep mode
    alarmoForceHeadphoneOut(false);

    while (/*!aptShouldClose() &&*/ !alarmoGetOut) {
        if (alarmoStop) {
            beepEndCb();
        }

        if (alarmoState) {
            if (alarmoCurTime != NULL) {
                if (alarmoRinging > 0) {
                    if (alarmoCurTime->tm_sec != lastsec) {
                        lastsec = alarmoCurTime->tm_sec;

                        if (alarmoRinging >= 600) {
                            alarmoRepeatAt = (osGetTime()/1000*1000)+300000;
                            alarmoRinging = 0;
                            if (alarmoShut) {
                                alarmoShut = false;
                            }
                            rgbl_changeLed(alarmoRgbEnd);
                        } else {
                            if (beepCb()) beepEndCb();
                        }
                    }
                }

                if (!alarmoRinging && (
                        (
                            alarmoCurTime->tm_hour == alarmoRingTime[0] &&
                            alarmoCurTime->tm_min == alarmoRingTime[1] &&
                            alarmoCurTime->tm_sec == 0
                        ) || (
                            alarmoRepeatAt != 0 &&
                            osGetTime()-alarmoRepeatAt < 10000
                        )
                    )
                ) {
                    if (alarmoRepeatAt != 0) alarmoRepeatAt = 0;
                    lastsec = alarmoCurTime->tm_sec;
                    if (beepCb()) beepEndCb();
                }
            }
        }
        svcSleepThread(1000000000 / 30);
    }
}