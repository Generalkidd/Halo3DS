#include "cseries/cseries.h"
#include "cseries/errors.h"
#include "bitmaps/bitmaps_inlines.h"
#include "interface/hud_draw.h"
#include "bitmap_rounding_vectors.h"

void n3ds_log(const char *message);

/* These run on ARM, including actual source inlines and the HUD sentinel. */
__attribute__((noinline)) int halo_engine_bitmap_tests(void)
{
    long return_eip = get_return_eip();
    long stack_buffer[STACK_BUFFER_LENGTH];
    unsigned long i;
    real_argb_color argb;
    real_rgb_color rgb;
    real channel;
#define CHECK(expr) do { if (!(expr)) { n3ds_log("BITMAP/HUD FAIL: " #expr); return 1; } } while (0)
    CHECK(return_eip != 0);
    CHECK(return_eip == (long)__builtin_extract_return_addr(__builtin_return_address(0)));
    csmemset(stack_buffer, 0x62, sizeof(stack_buffer));
    CHECK(check_stack_buffer(stack_buffer) == NONE);
    for (i = 0; i < STACK_BUFFER_LENGTH; ++i) {
        stack_buffer[i] ^= 1;
        CHECK(check_stack_buffer(stack_buffer) == (short)i);
        stack_buffer[i] ^= 1;
    }
    for (i = 0; i < sizeof(bitmap_rounding_vectors)/sizeof(bitmap_rounding_vectors[0]); ++i) {
        unsigned long expected = bitmap_rounding_vectors[i].expected;
        csmemcpy(&channel, &bitmap_rounding_vectors[i].bits, sizeof(channel));
        argb.alpha = channel; argb.red = .25f; argb.green = .5f; argb.blue = .75f;
        CHECK(real_argb_color_to_pixel32(&argb) == ((expected << 24) | 0x004080bfUL));
        argb.alpha = 1.f; argb.red = channel; argb.green = 0.f; argb.blue = 0.f;
        CHECK(real_argb_color_to_pixel32(&argb) == (0xff000000UL | (expected << 16)));
        argb.red = 0.f; argb.green = channel;
        CHECK(real_argb_color_to_pixel32(&argb) == (0xff000000UL | (expected << 8)));
        argb.green = 0.f; argb.blue = channel;
        CHECK(real_argb_color_to_pixel32(&argb) == (0xff000000UL | expected));
        rgb.red = channel; rgb.green = .5f; rgb.blue = 1.f;
        CHECK(real_rgb_color_to_pixel32(&rgb) == ((expected << 16) | 0x000080ffUL));
        CHECK(real_alpha_to_pixel32(channel) == (expected << 24));
    }
    match_assert_stack_frame(__FILE__, __LINE__);
    CHECK(return_eip == get_return_eip());
    n3ds_log("PASS: bitmap quantization boundaries, unsigned ARGB packing and all 128 HUD stack sentinels");
    return 0;
#undef CHECK
}
