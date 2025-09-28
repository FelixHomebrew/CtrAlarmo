#include <3ds/types.h>

typedef struct {
    uint8_t ani[4];
    uint8_t r[32];
    uint8_t g[32];
    uint8_t b[32];
} rgbl_McuLedPattern;

bool rgbl_changeLed(/*FnfHw::McuLedPattern*/ rgbl_McuLedPattern pat);