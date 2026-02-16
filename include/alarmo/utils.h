#include <3ds.h>

typedef struct {
    char* msg;
    u64 end;
} AlarmoTinyNotifCfg;

// Tiny notify
extern AlarmoTinyNotifCfg alarmoTinyNotifyCur;

void alarmoTinyNotify(char* msg, u8 sec);

/**
 * Wrapper to DSP:ForceHeadphoneOut
 * @param enable Should force Headphone output or not
 * 
 * Source: https://www.3dbrew.org/wiki/DSP:ForceHeadphoneOut
 */
Result alarmoForceHeadphoneOut(bool enable);