#include <3ds.h>

#define srwait(MS, ROUTINE) { \
    u32 cnt = 0; \
    while (!alarmoStop && cnt < MS) { \
        ROUTINE; \
        svcSleepThread(1000000); \
        cnt++; \
    } \
}

bool beepEmit(u16 freq, u16 ms, bool force);
bool beepCb();
void beepEndCb();