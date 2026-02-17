#include <alarmo/state.h>

#include <alarmo/routine.h>

bool alarmoState = true;

volatile bool alarmoShut = false;

vu16 alarmoRinging = 0;
vu64 alarmoRepeatAt = 0;

u8 alarmoSettingsStateCursor = 0;

struct tm* alarmoCurTime;

bool alarmoGetOut = false;