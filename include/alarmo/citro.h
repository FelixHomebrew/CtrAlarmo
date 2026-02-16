#include <citro2d.h>

#define HOUR_XPOS 200.0f
#define HOUR_YPOS 20.0f

// C2D helpers
extern C3D_RenderTarget* renderTop /*,*renderBot*/;

extern C2D_TextBuf
    hourBuf, hourSecBuf,
    alarmBuf,
    apTopBuf[2], apBotBuf[2],
    botBuf;

// Alarmo res
extern C2D_Font alarmoFontDefault, alarmoFontSystem;