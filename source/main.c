#include <3ds.h>
#include <citro2d.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "rgbl.h"

#define HOUR_XPOS 200.0f
#define HOUR_YPOS 20.0f

#define ASET_BEEP (1<<0)
#define ASET_RGBL (1<<1)
#define ASET_PWLB (1<<2)
#define ASET_PRLF (1<<3)
#define ASET_POBS (1<<4)
#define ASET_12HF (1<<5)
#define ASETNUM (sizeof(alarmoSettingsStr)/sizeof(char*))

// C2D helpers
C3D_RenderTarget* renderTop // ,*renderBot
;
C2D_TextBuf
    hourBuf, hourSecBuf,
    alarmBuf,
    apTopBuf[2], apBotBuf[2],
    botBuf;

// Alarmo config
const char* alarmoSettingsStr[] = {
    "Audible beep",
    "RGB LED blink",
    "Power & Wireless LEDs blink",
    "Progressive beep frequency",
    "Power off bottom screen",
    "12-hour time format"
};

u8 alarmoSettings = 0;
u8 alarmoRingTime[2] = {0}; // HH:MM

struct tm* alarmoCurTime;

bool alarmoState = true;


// Alarmo res
C2D_Font alarmoFontDefault, alarmoFontSystem;

// Tiny notify
struct {
    char* msg;
    u64 end;
} alarmoTinyNotifyCur = {0};
void alarmoTinyNotify(char* msg, u8 sec) {
    alarmoTinyNotifyCur.msg = msg;
    alarmoTinyNotifyCur.end = osGetTime() + 1000*sec;
}

// IO Helpers
#define alarmoPath "sdmc:/3ds/CTRAlarmo"
void alarmoIOWrite() {
    errorConf err;
    errorInit(&err, ERROR_TEXT, CFG_LANGUAGE_EN);
    errorText(&err, "Cannot write config.\nMake sure your SD card has a little free storage.");

    struct stat st;
    if (stat(alarmoPath, &st) != 0) {
        if (mkdir(alarmoPath, 0777) != 0) {
            errorDisp(&err);
            return;
        }
    }
    FILE* fp = fopen(alarmoPath "/config.bin", "wb");
    if (!fp) {
        errorDisp(&err);
        return;
    }

    size_t chk = 0;
    chk += fwrite(&alarmoSettings, 1, 1, fp);
    chk += fwrite(&alarmoRingTime, 2, 1, fp);
    fclose(fp);
    if (chk != 2) errorDisp(&err);
}
void alarmoIORead() {
    FILE* fp;
    if (!(fp = fopen(alarmoPath "/config.bin", "rb"))) {
        alarmoRingTime[0] = 6;
        alarmoRingTime[1] = 0;
        alarmoSettings = ASET_BEEP | ASET_RGBL | ASET_12HF;
        alarmoIOWrite();
    }
    if (!fp) {
        return;
    }
    fread(&alarmoSettings, 1, 1, fp);
    fread(&alarmoRingTime, 2, 1, fp);
    fclose(fp);
}

bool alarmoShut = false;

// Alarmo state
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

// Settings state
u8 alarmoSetStateCursor = 0;
void alarmoSettingsInit() {
    consoleClear();
    gspLcdInit();
    GSPLCD_PowerOnBacklight(GSPLCD_SCREEN_BOTTOM);
    gspLcdExit();
    alarmoState = false;
}
static void alarmoSetPrint(bool check, u8 ind, u8 flag, const char* text) {
    u8 spaLen = 40-strlen(text)-3;
    char* space = malloc(spaLen+1);
    memset(space, ' ', spaLen-1);
    space[spaLen-1] = '\0';

    printf(alarmoSetStateCursor == ind ? CONSOLE_RED : CONSOLE_ESC(107m));
    printf("%s%s", text, space);
    if (check) {
        printf("[%c]", alarmoSettings & flag ? 'X' : '-');
    } else {
        printf("   ");
    }
    printf("\n" CONSOLE_RESET);
    free(space);
}
static void alarmoSetSwitch(u8 flag) {
    if (alarmoSettings & flag) alarmoSettings &= ~flag;
    else alarmoSettings |= flag;
}

vu16 alarmoRinging = 0;
u64 alarmoRepeatAt = 0;

#define alarmoStop ((alarmoRinging || alarmoRepeatAt != 0) && alarmoShut)
#define srwait(MS, ROUTINE) { \
    u32 cnt = 0; \
    while (!alarmoStop && cnt < MS) { \
        ROUTINE; \
        svcSleepThread(1000000); \
        cnt++; \
    } \
}
rgbl_McuLedPattern alarmoRgbBeep0 = {
    {0x20, 0x00, 0xFF, 0x00},
    {0, 0xFF, 0},
    {0, 0xFF, 0},
    {0, 0xFF, 0}
};
/*rgbl_McuLedPattern alarmoRgbBeep1 = {
    {0x20, 0x00, 0xFF, 0x00},
    {0, 0xFF, 0, 0xFF, 0},
    {0, 0xFF, 0, 0xFF, 0},
    {0, 0xFF, 0, 0xFF, 0}
};
rgbl_McuLedPattern alarmoRgbBeep2 = {
    {0x20, 0x00, 0xFF, 0x00},
    {0, 0xFF, 0, 0xFF, 0, 0xFF, 0, 0xFF, 0},
    {0, 0xFF, 0, 0xFF, 0, 0xFF, 0, 0xFF, 0},
    {0, 0xFF, 0, 0xFF, 0, 0xFF, 0, 0xFF, 0}
};
rgbl_McuLedPattern alarmoRgbBeep3 = {
    {0x20, 0x00, 0xFF, 0x00},
    {0, 0xFF, 0, 0xFF, 0, 0xFF, 0, 0xFF, 0, 0xFF, 0, 0xFF, 0, 0xFF, 0, 0xFF, 0, 0xFF, 0, 0xFF, 0, 0xFF, 0, 0xFF, 0, 0xFF, 0, 0xFF, 0, 0xFF, 0, 0xFF},
    {0, 0xFF, 0, 0xFF, 0, 0xFF, 0, 0xFF, 0, 0xFF, 0, 0xFF, 0, 0xFF, 0, 0xFF, 0, 0xFF, 0, 0xFF, 0, 0xFF, 0, 0xFF, 0, 0xFF, 0, 0xFF, 0, 0xFF, 0, 0xFF},
    {0, 0xFF, 0, 0xFF, 0, 0xFF, 0, 0xFF, 0, 0xFF, 0, 0xFF, 0, 0xFF, 0, 0xFF, 0, 0xFF, 0, 0xFF, 0, 0xFF, 0, 0xFF, 0, 0xFF, 0, 0xFF, 0, 0xFF, 0, 0xFF}
};
Unused*/
rgbl_McuLedPattern alarmoRgbEnd = {
    {0xFF, 0xC0, 0xFF, 0x00},
    {0, 0x9F, 0},
    {0, 0x9F, 0},
    {0, 0x9F, 0}
};

#define BEEP1_STATE 20
#define BEEP2_STATE 40
#define BEEP3_STATE 60

#define BEEP_SAMPLERATE 32000
#define BEEP_BYTESPERSAMPLE 4
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
    if (alarmoSettings & (1<<3)) {
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
void beepECb() {
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
}

bool alarmoGetOut = false;

void mainPara(void*p) {
    u8 lastsec = 0;

    while (!aptShouldClose() && !alarmoGetOut) {
        if (alarmoStop) {
            beepECb();
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
                            if (beepCb()) beepECb();
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
                    if (beepCb()) beepECb();
                }
            }
        }
        svcSleepThread(1000000000 / 30);
    }
}

aptHookCookie cookie;
static void aptHookFunc(APT_HookType hookType, void* param) {
    switch (hookType) {
        case APTHOOK_ONSUSPEND:
            if (alarmoRinging) alarmoShut = true;
            gspLcdInit();
            GSPLCD_PowerOnAllBacklights();
            gspLcdExit();
            break;
        case APTHOOK_ONRESTORE:
        case APTHOOK_ONWAKEUP:
            if (alarmoRinging) alarmoShut = true;
            if (alarmoSettings & ASET_POBS && alarmoState) {
                gspLcdInit();
                GSPLCD_PowerOffBacklight(GSPLCD_SCREEN_BOTTOM);
                gspLcdExit();
            }
            break;
        case APTHOOK_ONSLEEP:
            if (alarmoRinging) alarmoShut = true;
            break;
        default:
            break;
    }
}

int main() {
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(4096);
    C2D_Prepare();

    consoleInit(GFX_BOTTOM, NULL);

    aptInit();
    aptSetSleepAllowed(false);
    APT_SetAppCpuTimeLimit(30);

    aptHook(&cookie, aptHookFunc, NULL);

    ndmuInit();
    NDMU_EnterExclusiveState(NDM_EXCLUSIVE_STATE_INFRASTRUCTURE);
    NDMU_LockState();

    fsInit();
    romfsInit();
    ndspInit();

    renderTop = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    //renderBot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

    alarmoFontDefault = C2D_FontLoad("romfs:/fnt/G7Segment7S5.bcfnt");
    alarmoFontSystem = C2D_FontLoadSystem(CFG_LANGUAGE_EN);

    alarmoIORead();
    alarmoMainInit();

    Thread mainParamT;
    {
        s32 prio;
        svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
        mainParamT = threadCreate(mainPara, NULL, 0x1000, prio-0x10, 0, true);
    }

    //u64 lastOt = U64_MAX;
    while (aptMainLoop()) {
        hidScanInput();

        u32 kDown = hidKeysDown();

        if (alarmoRinging || alarmoRepeatAt != 0) {
            if (kDown) alarmoShut = true;
            //while (alarmoRinging != 0);
        }

        if (!alarmoState) {
            printf("\x1b[1;1H");
            for (u8 i = 0; i < ASETNUM; i++)
                alarmoSetPrint(true, i, 1<<i, alarmoSettingsStr[i]);
            alarmoSetPrint(false, ASETNUM, 0, "Redefine alarm");

            printf("\x1b[30;1HPress B or SELECT to return");

            if (kDown & KEY_UP && alarmoSetStateCursor > 0) alarmoSetStateCursor--;
            else if (kDown & KEY_DOWN && alarmoSetStateCursor < ASETNUM) alarmoSetStateCursor++;
            else if (kDown & KEY_A) {
                if (alarmoSetStateCursor >= ASETNUM) {
                    char oh[3], om[3], nh[3], nm[3];
                    long rh = 0, rm = 0;
                    char* endptr;

                    snprintf(oh, sizeof(oh), "%hhu", alarmoRingTime[0]);
                    snprintf(om, sizeof(om), "%hhu", alarmoRingTime[1]);

                    SwkbdState kb;
                    swkbdInit(&kb, SWKBD_TYPE_NUMPAD, 2, 2);
                    
                    askhour:; {
                        errno = 0;
                        swkbdSetInitialText(&kb, oh);
                        swkbdSetHintText(&kb, "Type hour (24-hour format)");
                        SwkbdButton btn;
                        switch (btn = swkbdInputText(&kb, nh, sizeof(nh))) {
                            case SWKBD_BUTTON_RIGHT:
                                rh = strtol(nh, &endptr, 10);
                                if (errno || *endptr != '\0' || rh < 0 || rh > 23)
                                    goto askhour;
                                break;
                            default:
                                goto end;
                        }
                    }
                    askmin:; {
                        errno = 0;
                        swkbdSetInitialText(&kb, om);
                        swkbdSetHintText(&kb, "Type minute");
                        switch (swkbdInputText(&kb, nm, sizeof(nm))) {
                            case SWKBD_BUTTON_RIGHT:
                                rm = strtol(nm, &endptr, 10);
                                if (errno || *endptr != '\0' || rm < 0 || rm > 59)
                                    goto askmin;
                                break;
                            default:
                                goto end;
                        }
                    }
                    alarmoRingTime[0] = (u8)(rh % 24);
                    alarmoRingTime[1] = (u8)(rm % 60);
                    end:;
                } else {
                    alarmoSetSwitch(1<<alarmoSetStateCursor);
                }
            }
            else if (kDown & KEY_B || kDown & KEY_SELECT) {
                printf("\x1b[30;1H\033[2K\r" CONSOLE_ESC(33m) "Saving new config, please wait...");
                alarmoIOWrite();
                alarmoMainInit();
                continue;
            }
        }

        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C2D_TargetClear(renderTop, C2D_Color32(0x00,0x00,0x00,0xFF));
		C2D_SceneBegin(renderTop);
		{

            if (!alarmoRinging || (alarmoRinging && osGetTime()%1000 < 500) || alarmoShut) {
                time_t unixTime = time(NULL);
                alarmoCurTime = gmtime((const time_t*)&unixTime);

                {
                    C2D_TextBufClear(hourBuf);
                    C2D_Text hourText;
                    char hourChar[6];
                    if (alarmoSettings & ASET_12HF)
                        snprintf(hourChar, sizeof(hourChar), "%02hhu:%02hhu", alarmoCurTime->tm_hour % 12 == 0 ? 12 : alarmoCurTime->tm_hour % 12, alarmoCurTime->tm_min);
                    else
                        snprintf(hourChar, sizeof(hourChar), "%02hhu:%02hhu", alarmoCurTime->tm_hour, alarmoCurTime->tm_min);
                    C2D_TextFontParse(&hourText, alarmoFontDefault, hourBuf, hourChar);
                    C2D_TextOptimize(&hourText);
                    C2D_DrawText(
                        &hourText,
                        C2D_AlignCenter | C2D_WithColor,
                        HOUR_XPOS, HOUR_YPOS, 0.5f,
                        3.0f, 3.0f,
                        C2D_Color32f(1,1,1,1)
                    );
                }
                {
                    C2D_TextBufClear(hourSecBuf);
                    C2D_Text hourSecText;
                    char hourSecChar[6];
                    snprintf(hourSecChar, sizeof(hourSecChar), "%02hhu", alarmoCurTime->tm_sec);
                    C2D_TextFontParse(&hourSecText, alarmoFontDefault, hourSecBuf, hourSecChar);
                    C2D_TextOptimize(&hourSecText);
                    C2D_DrawText(
                        &hourSecText,
                        C2D_AlignLeft | C2D_WithColor,
                        HOUR_XPOS+120.0f, HOUR_YPOS+47.0f, 0.5f,
                        1.0f, 1.0f,
                        C2D_Color32f(1,1,1,1)
                    );
                }

                if (alarmoSettings & ASET_12HF) {
                    C2D_TextBufClear(apTopBuf[0]);
                    C2D_Text apTopText;
                    char apTopChar[6];
                    snprintf(apTopChar, sizeof(apTopChar), "%cM", alarmoCurTime->tm_hour > 11 ? 'P' : 'A');
                    C2D_TextFontParse(&apTopText, alarmoFontDefault, apTopBuf[0], apTopChar);
                    C2D_TextOptimize(&apTopText);
                    C2D_DrawText(
                        &apTopText,
                        C2D_AlignLeft | C2D_WithColor,
                        HOUR_XPOS+120.0f, HOUR_YPOS+8.0f, 0.5f,
                        1.0f, 1.0f,
                        C2D_Color32f(1,1,1,1)
                    );
                }
                {
                    C2D_TextBufClear(apTopBuf[1]);
                    C2D_Text apTopText;
                    char apTopChar[6];
                    snprintf(apTopChar, sizeof(apTopChar), "HO");
                    C2D_TextFontParse(&apTopText, alarmoFontDefault, apTopBuf[1], apTopChar);
                    C2D_TextOptimize(&apTopText);
                    C2D_DrawText(
                        &apTopText,
                        C2D_AlignLeft | C2D_WithColor,
                        HOUR_XPOS-154.0f, HOUR_YPOS+47.0f, 0.5f,
                        1.0f, 1.0f,
                        C2D_Color32f(1,1,1,1)
                    );
                }

                {
                    C2D_TextBufClear(alarmBuf);
                    C2D_Text alarmText;
                    char alarmChar[6];
                    if (alarmoSettings & ASET_12HF)
                        snprintf(alarmChar, sizeof(alarmChar), "%02hhu:%02hhu", alarmoRingTime[0] % 12 == 0 ? 12 : alarmoRingTime[0] % 12, alarmoRingTime[1]);
                    else
                        snprintf(alarmChar, sizeof(alarmChar), "%02hhu:%02hhu", alarmoRingTime[0], alarmoRingTime[1]);
                    C2D_TextFontParse(&alarmText, alarmoFontDefault, alarmBuf, alarmChar);
                    C2D_TextOptimize(&alarmText);
                    C2D_DrawText(
                        &alarmText,
                        C2D_AlignCenter | C2D_WithColor,
                        HOUR_XPOS, HOUR_YPOS+100.0f, 0.5f,
                        3.0f, 3.0f,
                        C2D_Color32f(1,1,1,1)
                    );
                }
                if (alarmoSettings & ASET_12HF) {
                    C2D_TextBufClear(apBotBuf[0]);
                    C2D_Text apBotText;
                    char apBotChar[6];
                    snprintf(apBotChar, sizeof(apBotChar), "%cM", alarmoRingTime[0] > 11 ? 'P' : 'A');
                    C2D_TextFontParse(&apBotText, alarmoFontDefault, apBotBuf[0], apBotChar);
                    C2D_TextOptimize(&apBotText);
                    C2D_DrawText(
                        &apBotText,
                        C2D_AlignLeft | C2D_WithColor,
                        HOUR_XPOS+120.0f, HOUR_YPOS+108.0f, 0.5f,
                        1.0f, 1.0f,
                        C2D_Color32f(1,1,1,1)
                    );
                }
                {
                    C2D_TextBufClear(apBotBuf[1]);
                    C2D_Text apBotText;
                    char apBotChar[6];
                    snprintf(apBotChar, sizeof(apBotChar), "AL");
                    C2D_TextFontParse(&apBotText, alarmoFontDefault, apBotBuf[1], apBotChar);
                    C2D_TextOptimize(&apBotText);
                    C2D_DrawText(
                        &apBotText,
                        C2D_AlignLeft | C2D_WithColor,
                        HOUR_XPOS-154.0f, HOUR_YPOS+147.0f, 0.5f,
                        1.0f, 1.0f,
                        C2D_Color32f(1,1,1,1)
                    );
                }
            }
            {
                C2D_TextBufClear(botBuf);
                C2D_Text botText;
                char botChar[21];
                if (alarmoRinging)
                    snprintf(botChar, sizeof(botChar), "Time up!");
                else if (alarmoRepeatAt != 0) {
                    u32 remainS = (u32)((alarmoRepeatAt-osGetTime()) / 1000);
                    snprintf(botChar, sizeof(botChar), "Repeat in: %02hhu:%02hhu", (u8)(remainS/60), (u8)(remainS%60));
                } else if (osGetTime() < alarmoTinyNotifyCur.end) {
                    snprintf(botChar, sizeof(botChar), alarmoTinyNotifyCur.msg);
                } else {
                    const u32 secdt = (
                          (alarmoRingTime[0]      * 3600 + alarmoRingTime[1]     * 60 + 0)
                        - (alarmoCurTime->tm_hour * 3600 + alarmoCurTime->tm_min * 60 + alarmoCurTime->tm_sec)
                        + (60*60*24)
                    ) % (60*60*24);
                    snprintf(
                        botChar, sizeof(botChar),
                        "%02hhu:%02hhu:%02hhu",
                        (u8)(secdt / 3600),
                        (u8)((secdt % 3600) / 60),
                        (u8)(secdt % 60)
                    );
                }
                C2D_TextFontParse(&botText, alarmoFontSystem, botBuf, botChar);
                C2D_TextOptimize(&botText);
                C2D_DrawText(
                    &botText,
                    C2D_AlignCenter | C2D_WithColor,
                    HOUR_XPOS, HOUR_YPOS+190, 0.5f,
                    0.5f, 0.5f,
                    C2D_Color32f(1,1,1,1)
                );
            }
        }
        if (alarmoState) {
            //C2D_TargetClear(renderBot, C2D_Color32(0x00,0x80,0xFF,0xFF));
            //C2D_SceneBegin(renderBot);
            if ((kDown & KEY_UP) || (kDown & KEY_DOWN)) {
                u32 tb, bb;
                gspLcdInit();
                GSPLCD_GetBrightness(GSPLCD_SCREEN_TOP, &tb);
                GSPLCD_GetBrightness(GSPLCD_SCREEN_BOTTOM, &bb);

                u32 nb = (u32)( (float)(tb > bb ? tb : bb) * (kDown & KEY_UP ? 2.0f : 0.5f) );
                //if (nb > 5) nb = 5;
                if (nb < 1) nb = 1;

                GSPLCD_SetBrightnessRaw(GSPLCD_SCREEN_TOP, nb);
                GSPLCD_SetBrightnessRaw(GSPLCD_SCREEN_BOTTOM, nb);
                gspLcdExit();
            }
            if (kDown & KEY_START && envIsHomebrew()) break;
            if (kDown & KEY_SELECT) {
                alarmoSettingsInit();
                continue;
            }
        }
		C3D_FrameEnd(0);
    }

    alarmoGetOut = true;
    threadJoin(mainParamT, U64_MAX);

    gspLcdInit();
    GSPLCD_PowerOnAllBacklights();
    gspLcdExit();

    ndspExit();
    romfsExit();
    fsExit();

    NDMU_UnlockState();
    NDMU_LeaveExclusiveState();
    ndmuExit();

    aptUnhook(&cookie);
    aptExit();

    C2D_Fini();
    gfxExit();

    return 0;
}