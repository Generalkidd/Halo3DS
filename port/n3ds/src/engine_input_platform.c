#include <3ds.h>
#include "engine_input.h"

#define VERIFY_KEY(name) _Static_assert(NATIVE_KEY_##name==KEY_##name, "HID key ABI: " #name)
VERIFY_KEY(A); VERIFY_KEY(B); VERIFY_KEY(SELECT); VERIFY_KEY(START);
VERIFY_KEY(DRIGHT); VERIFY_KEY(DLEFT); VERIFY_KEY(DUP); VERIFY_KEY(DDOWN);
VERIFY_KEY(R); VERIFY_KEY(L); VERIFY_KEY(X); VERIFY_KEY(Y);
VERIFY_KEY(ZL); VERIFY_KEY(ZR); VERIFY_KEY(TOUCH);

int n3ds_input_platform_initialize(void) { return R_SUCCEEDED(hidInit()); }
void n3ds_input_platform_dispose(void) { hidExit(); }
void n3ds_input_platform_poll(struct native_input_sample *sample)
{
    circlePosition circle={0}, cstick={0};
    touchPosition touch={0};
    hidScanInput(); /* Also scans IRRST for the New 3DS C-stick and ZL/ZR. */
    hidCircleRead(&circle); hidCstickRead(&cstick); hidTouchRead(&touch);
    sample->held=hidKeysHeld();
    sample->circle_x=circle.dx; sample->circle_y=circle.dy;
    sample->cstick_x=cstick.dx; sample->cstick_y=cstick.dy;
    sample->touch_x=touch.px; sample->touch_y=touch.py;
}
