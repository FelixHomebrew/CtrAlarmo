#include <alarmo/settings.h>

#include <stdio.h>
#include <sys/stat.h>

#include <alarmo/state.h>

// App data
#define alarmoPath "sdmc:/3ds/CTRAlarmo"

const char* alarmoSettingsStr[ASETNUM] = {
    "Audible beep",
    "RGB LED blink",
    "Power & Wireless LEDs blink",
    "Progressive beep frequency",
    "Power off bottom screen",
    "12-hour time format"
};

u8 alarmoSettings = 0;
u8 alarmoRingTime[2] = {0};

// IO Helpers
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

void alarmoSettingsInit() {
    consoleClear();
    gspLcdInit();
    GSPLCD_PowerOnBacklight(GSPLCD_SCREEN_BOTTOM);
    gspLcdExit();
    alarmoState = false;
}