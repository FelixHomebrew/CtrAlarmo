#include <3ds.h>

#define ASET_BEEP (1<<0) // Beep
#define ASET_RGBL (1<<1) // RGB LED
#define ASET_PWLB (1<<2) // Power/wireless LEDs
#define ASET_PRLF (1<<3) // Progressive beep
#define ASET_POBS (1<<4) // Power off bottom screen
#define ASET_12HF (1<<5) // 12-hour format
#define ASETNUM 6

// Alarmo config
extern const char* alarmoSettingsStr[ASETNUM];

extern u8 alarmoSettings;
extern u8 alarmoRingTime[2]; // HH:MM

void alarmoIOWrite();
void alarmoIORead();

void alarmoSettingsInit();