#include <alarmo/citro.h>

C3D_RenderTarget* renderTop /*,*renderBot*/;

C2D_TextBuf
    hourBuf, hourSecBuf,
    alarmBuf,
    apTopBuf[2], apBotBuf[2],
    botBuf;

C2D_Font alarmoFontDefault, alarmoFontSystem;