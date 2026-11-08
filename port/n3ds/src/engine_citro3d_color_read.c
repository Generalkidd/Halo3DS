/* The pinned Citro3D context records framebuffer and fragment state before
 * each draw. Keep its framebuffer shadow intact and track the hardware
 * color-read permission across runs of eligible indexed draws. */
#if defined(HALO_N3DS_OPAQUE_COLOR_READS) || defined(HALO_N3DS_UNUSED_TEXTURE_UNITS)
#include <3ds.h>
#include "internal.h"

void __real_C3Di_UpdateContext(void);
#ifdef HALO_N3DS_UNUSED_TEXTURE_UNITS
unsigned int n3ds_gpu_texture_unit_mask(void);
void n3ds_gpu_texture_unit_observe(unsigned int omitted, int write);
/* Shadow only the actual enable bits, never the retained descriptors. The
 * stock context still owns all texture bindings and cache invalidations. */
static unsigned int installed_texture_units = 7;
static void texture_units_update(const C3D_Context *context, bool stock_write)
{
    unsigned int stock = context->texConfig & 7u;
    unsigned int desired = stock & n3ds_gpu_texture_unit_mask();
    if (stock_write) installed_texture_units = stock;
    int write = installed_texture_units != desired;
    if (write) {
        GPUCMD_AddMaskedWrite(GPUREG_TEXUNIT_CONFIG, 1,
            (context->texConfig & ~7u) | desired);
        /* Follow the pinned SDK's configure-then-invalidate ordering when
         * changing enabled units, including restoration without a rebind. */
        GPUCMD_AddMaskedWrite(GPUREG_TEXUNIT_CONFIG, 4, BIT(16));
        installed_texture_units = desired;
    }
    n3ds_gpu_texture_unit_observe(stock & ~desired, write);
}
#endif

#ifdef HALO_N3DS_OPAQUE_COLOR_READS
int n3ds_gpu_opaque_color_read_path(const void *color_buffer);
void n3ds_gpu_opaque_color_read_draw(int omitted);
void n3ds_gpu_opaque_color_read_transition(int extra_flush);
void __real_C3D_DrawElements(GPU_Primitive_t primitive, int count, int type, const void *indices);

enum { COLOR_READ_CANDIDATE = 2 };
/* Only the owner thread records draws. The path is set solely inside
 * __wrap_C3D_DrawElements. The permission tracks the recorded command stream.
 * A dirty framebuffer bind restores the stock permission after a target
 * change or resume, and a frame split flushes and invalidates the cache. */
static int color_read_path;
static bool color_read_omitted;

static bool overwrites_all_color(const C3D_Context *context)
{
    const C3D_Effect *effect = &context->effect;
    /* These fields use the pinned Citro3D effect.c encoding. Ordinary
     * additive blend with ONE/ZERO copies the source, including alpha. */
    const u32 overwrite_blend = (u32)GPU_BLEND_ADD | ((u32)GPU_BLEND_ADD << 8) |
        ((u32)GPU_ONE << 16) | ((u32)GPU_ZERO << 20) |
        ((u32)GPU_ONE << 24) | ((u32)GPU_ZERO << 28);
    const u32 opaque_depth = 1u | ((u32)GPU_GEQUAL << 4) | ((u32)GPU_WRITE_ALL << 8);
    return context->fb.colorBuf && context->fb.colorFmt == GPU_RB_RGBA8 &&
        context->fb.colorMask == GPU_WRITE_COLOR &&
        context->fb.depthBuf && context->fb.depthFmt == GPU_RB_DEPTH24_STENCIL8 &&
        context->fb.depthMask == 3 && effect->depthTest == opaque_depth &&
        (effect->fragOpMode & 0xFFFFu) == (0x0100u | GPU_FRAGOPMODE_GL) &&
        effect->alphaBlend == overwrite_blend &&
        (context->texEnvBuf & 7u) != GPU_GAS;
}

static void color_read_update(void)
{
    if (!color_read_path && !color_read_omitted) {
        __real_C3Di_UpdateContext();
        return;
    }

    const C3D_Context *context = C3Di_GetContext();
    const bool framebuffer_dirty = (context->flags & C3DiF_FrameBuf) != 0;
    const bool eligible = color_read_path && overwrites_all_color(context);
    const bool omit_read = eligible && color_read_path == COLOR_READ_CANDIDATE;
    const bool extra_flush = !framebuffer_dirty && color_read_omitted != omit_read;
    /* Access changes require a flush followed by cache invalidation.
     * For a target rebind Citro3D performs that barrier itself. Otherwise
     * order the barrier before stock effect-register writes. */
    if (extra_flush) {
        GPUCMD_AddWrite(GPUREG_FRAMEBUFFER_FLUSH, 1);
        GPUCMD_AddWrite(GPUREG_FRAMEBUFFER_INVALIDATE, 1);
    }
    __real_C3Di_UpdateContext();

    /* A dirty bind installs the stock permissions. In that case the new
     * access mask can follow in the same setup, before any primitive. */
    if (framebuffer_dirty)
        color_read_omitted = false;
    if (color_read_omitted != omit_read) {
        const u32 stock_read = context->fb.colorBuf ? context->fb.colorMask : 0;
        GPUCMD_AddWrite(GPUREG_COLORBUFFER_READ, omit_read ? 0 : stock_read);
        color_read_omitted = omit_read;
        n3ds_gpu_opaque_color_read_transition(extra_flush);
    }
    if (eligible)
        n3ds_gpu_opaque_color_read_draw(omit_read);
}

void __wrap_C3D_DrawElements(GPU_Primitive_t primitive, int count, int type, const void *indices)
{
    color_read_path = n3ds_gpu_opaque_color_read_path(C3Di_GetContext()->fb.colorBuf);
    __real_C3D_DrawElements(primitive, count, type, indices);
    color_read_path = 0;
}
#endif

void __wrap_C3Di_UpdateContext(void)
{
#ifdef HALO_N3DS_UNUSED_TEXTURE_UNITS
    const C3D_Context *context = C3Di_GetContext();
    bool stock_texture_write = (context->flags & (C3DiF_TexAll | C3DiF_TexStatus)) != 0;
#endif
#ifdef HALO_N3DS_OPAQUE_COLOR_READS
    color_read_update();
#else
    __real_C3Di_UpdateContext();
#endif
#ifdef HALO_N3DS_UNUSED_TEXTURE_UNITS
    texture_units_update(context, stock_texture_write);
#endif
}
#endif
