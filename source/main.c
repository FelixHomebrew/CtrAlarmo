#include <3ds.h>
#include <citro2d.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
//#include <string.h>

#include <alarmo/beeper.h>
#include <alarmo/citro.h>
#include <alarmo/rgbl.h>
#include <alarmo/routine.h>
#include <alarmo/settings.h>
#include <alarmo/state.h>
#include <alarmo/utils.h>

static void alarmoSetPrint(bool check, u8 ind, u8 flag, const char* text) {
    u8 spaLen = 40-strlen(text)-3;
    char* space = malloc(spaLen+1);
    memset(space, ' ', spaLen-1);
    space[spaLen-1] = '\0';

    printf(alarmoSettingsStateCursor == ind ? CONSOLE_ESC(100;91m) : CONSOLE_ESC(37m));
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

PtmSleepConfig ptmEvCfg;

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
            if (alarmoRinging) alarmoShut = true;
            if (alarmoSettings & ASET_POBS && alarmoState) {
                gspLcdInit();
                GSPLCD_PowerOffBacklight(GSPLCD_SCREEN_BOTTOM);
                gspLcdExit();
            }
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

    // Ensure mainPara is active while sleep mode
    aptSetSleepAllowed(false);

    APT_SetAppCpuTimeLimit(4);

    ndmuInit();

    aptHook(&cookie, aptHookFunc, NULL);

    fsInit();
    romfsInit();
    ndspInit();

    renderTop = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    //renderBot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

    alarmoFontDefault = C2D_FontLoad("romfs:/fnt/G7Segment7S5.bcfnt");
    alarmoFontSystem = C2D_FontLoadSystem(CFG_LANGUAGE_EN);

    // Avoid sleep mode when closing shell
    /*mcuHwcInit(); //
    u32 im;
    MCUHWC_ReadRegister(0x18, &im, 4);
    im |= 1<<5;
    MCUHWC_WriteRegister(0x18, &im, 4);
    mcuHwcExit();*/

    alarmoIORead();
    alarmoMainInit();

    svcCreateMutex(&alarmoMutex, false);

    Thread mainParamT;
    {
        s32 prio;
        svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
        // Tried core 1 but is haltedon sleep...
        mainParamT = threadCreate(mainPara, NULL, 0x1000, prio+0x8, 0, true);
    }

    u8 shell[2] = {1};
    u64 osTime = 0;
    while (aptMainLoop()) {
        {
            time_t unixTime = time(NULL);
            alarmoCurTime = gmtime((const time_t*)&unixTime);
            osTime = osGetTime();
        }
        hidScanInput();
        u32 kDown = hidKeysDown();
        if (alarmoRinging || alarmoRepeatAt != 0) {
            if (kDown) {
                alarmoShut = true;
            }
        }

        // Checks for lid (Prevents BEEP?)
        ptmuInit();
        PTMU_GetShellState(&shell[0]);
        ptmuExit();
        if (shell[0] != shell[1]) {
            if (shell[0]) {
                // Disables Streetpass state + Restore power LED state
                if (!(alarmoRinging && alarmoSettings & ASET_PWLB)) {
                    mcuHwcInit();
                    u8 mcuPWP = 0;
                    MCUHWC_WriteRegister(0x29, &mcuPWP, 1);
                    mcuHwcExit();
                }
                gspLcdInit();
                if (alarmoSettings & ASET_POBS) {
                    GSPLCD_PowerOffBacklight(GSPLCD_SCREEN_BOTTOM);
                }
                gspLcdExit();

                NDMU_LeaveExclusiveState();
            } else {
                // Enable Streetpass state + Power LED sleep animation
                if (!(alarmoRinging && alarmoSettings & ASET_PWLB)) {
                    mcuHwcInit();
                    u8 mcuPWP = 2;
                    MCUHWC_WriteRegister(0x29, &mcuPWP, 1);
                    mcuHwcExit();
                }
                NDMU_EnterExclusiveState(NDM_EXCLUSIVE_STATE_STREETPASS);
            }
        }
        shell[1] = shell[0];
        if (!shell[0]) {
            // Thread sleep caused glitchy beeps... :/
            C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
            C3D_FrameEnd(0);
            continue;
        }

        if (!alarmoState) {
            printf("\x1b[1;1H");
            for (u8 i = 0; i < ASETNUM; i++)
                alarmoSetPrint(true, i, 1<<i, alarmoSettingsStr[i]);
            alarmoSetPrint(false, ASETNUM, 0, "Redefine alarm");

            printf("\x1b[30;1HPress B or SELECT to return");

            if (kDown & KEY_UP && alarmoSettingsStateCursor > 0) alarmoSettingsStateCursor--;
            else if (kDown & KEY_DOWN && alarmoSettingsStateCursor < ASETNUM) alarmoSettingsStateCursor++;
            else if (kDown & KEY_A) {
                if (alarmoSettingsStateCursor >= ASETNUM) {
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
                    alarmoSetSwitch(1<<alarmoSettingsStateCursor);
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
        C2D_TargetClear(renderTop, C2D_Color32(0x00, 0x00,0x00,0xFF));
		C2D_SceneBegin(renderTop);
		{
            if (!alarmoRinging || (alarmoRinging && osTime%1000 < 500) || alarmoShut) {
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
                svcWaitSynchronization(alarmoMutex, U64_MAX);
                C2D_TextBufClear(botBuf);
                C2D_Text botText;
                char botChar[21];
                if (alarmoRinging)
                    snprintf(botChar, sizeof(botChar), "Time up!");
                else if (alarmoRepeatAt != 0) {
                    u32 remainS = (u32)((alarmoRepeatAt-osTime) / 1000);
                    snprintf(botChar, sizeof(botChar), "Repeat in: %02hhu:%02hhu", (u8)(remainS/60), (u8)(remainS%60));
                } else if (osTime < alarmoTinyNotifyCur.end) {
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
                svcReleaseMutex(alarmoMutex);
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

    // Restore sleep mode
    /*mcuHwcInit();
    MCUHWC_ReadRegister(0x18, &im, 4);
    im &= ~(1<<5);
    MCUHWC_WriteRegister(0x18, &im, 4);
    mcuHwcExit();*/

    alarmoGetOut = true;
    threadJoin(mainParamT, U64_MAX);

    svcCloseHandle(alarmoMutex);

    gspLcdInit();
    GSPLCD_PowerOnAllBacklights();
    gspLcdExit();

    ndspExit();
    romfsExit();
    fsExit();
    
    //ptmSysmExit();
    ndmuExit();

    aptUnhook(&cookie);
    aptSetSleepAllowed(true);
    aptExit();

    C2D_Fini();
    gfxExit();

    return 0;
}