#include <alarmo/utils.h>

#include <alarmo/routine.h>

AlarmoTinyNotifCfg alarmoTinyNotifyCur = {0};

void alarmoTinyNotify(char* msg, u8 sec) {
    svcWaitSynchronization(alarmoMutex, U64_MAX);
    alarmoTinyNotifyCur.msg = msg;
    alarmoTinyNotifyCur.end = osGetTime() + 1000*sec;
    svcReleaseMutex(alarmoMutex);
}
#include <stdio.h>
Result alarmoForceHeadphoneOut(bool enable)
{
    Result res;

    Handle dspHandle;
    if (R_FAILED(res = srvGetServiceHandle(&dspHandle, "dsp::DSP"))) return res;

    u32* ipc = getThreadCommandBuffer();

    ipc[0] = IPC_MakeHeader(0x20, 1, 0);
    ipc[1] = enable ? 1 : 0;

    if (R_FAILED(res = svcSendSyncRequest(dspHandle))) return res;

    return ipc[1];
}