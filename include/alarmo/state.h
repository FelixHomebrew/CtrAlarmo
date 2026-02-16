#include <3ds.h>
#include <stdbool.h>

#define alarmoStop ((alarmoRinging || alarmoRepeatAt != 0) && alarmoShut)

extern bool alarmoState;

extern volatile bool alarmoShut;

extern vu16 alarmoRinging;
extern vu64 alarmoRepeatAt;

extern u8 alarmoSettingsStateCursor;

extern struct tm* alarmoCurTime;

extern bool alarmoGetOut;