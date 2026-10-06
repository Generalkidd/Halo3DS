#include <3ds.h>
#include <citro3d.h>
#include "engine_renderer.h"

int n3ds_gpu_framebuffer_blend(int mode)
{
    GPU_BLENDEQUATION operation=GPU_BLEND_ADD;
    GPU_BLENDFACTOR source_color=GPU_ONE,destination_color=GPU_ONE;
    GPU_BLENDFACTOR source_alpha=GPU_ONE,destination_alpha=GPU_ONE;
    /* Exact factors/operations from the original Xbox framebuffer blend table.
     * PICA exposes alpha factors separately; color factors on Xbox apply to
     * the alpha components too, so DST_COLOR/SRC_COLOR become DST/SRC_ALPHA. */
    switch(mode) {
    case NATIVE_BLEND_ALPHA:
        source_color=source_alpha=GPU_SRC_ALPHA;
        destination_color=destination_alpha=GPU_ONE_MINUS_SRC_ALPHA; break;
    case NATIVE_BLEND_MULTIPLY:
        source_color=GPU_DST_COLOR; source_alpha=GPU_DST_ALPHA;
        destination_color=destination_alpha=GPU_ZERO; break;
    case NATIVE_BLEND_DOUBLE_MULTIPLY:
        source_color=GPU_DST_COLOR; source_alpha=GPU_DST_ALPHA;
        destination_color=GPU_SRC_COLOR; destination_alpha=GPU_SRC_ALPHA; break;
    case NATIVE_BLEND_ADD: break;
    case NATIVE_BLEND_SUBTRACT: operation=GPU_BLEND_REVERSE_SUBTRACT; break;
    case NATIVE_BLEND_MIN: operation=GPU_BLEND_MIN; break;
    case NATIVE_BLEND_MAX: operation=GPU_BLEND_MAX; break;
    case NATIVE_BLEND_PREMULTIPLIED_ALPHA:
        destination_color=destination_alpha=GPU_ONE_MINUS_SRC_ALPHA; break;
    default: return 0; /* Invalid requests must not change the previous state. */
    }
    C3D_AlphaBlend(operation,operation,source_color,destination_color,source_alpha,destination_alpha);
    return 1;
}
