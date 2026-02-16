#include <alarmo/mcuHwc.h>

void alarmoMcuSetInterrupt(u8 bit, bool state) {
    mcuHwcInit(); // Restore reg
    u32 im;
    MCUHWC_ReadRegister(0x18, &im, 4);
    if (state) im |= 1<<bit; else im &= ~(1<<bit);
    MCUHWC_WriteRegister(0x18, &im, 4);
    mcuHwcExit();
}