#include "rgbl.h"
#include <3ds.h>
#include <string.h>

/**
 * Implementation of real-time RGB LED changer from CtrRgbPatty source
   Huge credits to CPunch who's behind this
 */

bool setPattern(/*FnfHw::McuLedPattern*/ rgbl_McuLedPattern pat) {
    Handle srvHandle = 0;
    if (srvGetServiceHandle(&srvHandle, "ptm:sysm") != 0) return false;
    u32* ipc = getThreadCommandBuffer();
    ipc[0] = 0x8010640;
    memcpy(&ipc[1], &pat, 0x64);
    svcSendSyncRequest(srvHandle);
    svcCloseHandle(srvHandle);
    return true;
}

extern u8 alarmoSettings;
bool rgbl_changeLed(/*FnfHw::McuLedPattern*/ rgbl_McuLedPattern pat) {
    if (alarmoSettings & 1<<1) {
        if (!setPattern(pat)) return false;
    }
    return true;

}
