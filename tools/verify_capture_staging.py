#!/usr/bin/env python3
"""Run capture and texture staging ownership checks with host UBSan."""

import argparse
from pathlib import Path
import subprocess
import tempfile


PORT = Path(__file__).resolve().parents[1] / 'port/n3ds'

# Compile the production bodies against narrowly scoped platform substitutes.
CAPTURE_PLATFORM = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t u32;
typedef uint64_t u64;
typedef int Result;
typedef struct {float m[16];} C3D_Mtx;
typedef struct {
    void *colorBuf, *depthBuf;
    unsigned int width, height, colorFmt, depthFmt, block32;
} C3D_FrameBuf;
struct native_render_camera {
    float position[3], forward[3], up[3], vertical_fov, near_clip, far_clip;
    unsigned int clear_color;
    int suppress_clear;
};
typedef struct {int numEntries;} gxCmdQueue_s;
typedef struct {gxCmdQueue_s gxQueue;} C3D_Context;
enum {GPU_RB_RGBA8 = 0, GPU_RB_DEPTH24_STENCIL8 = 3, SYSCLOCK_ARM11 = 2};
static C3D_Context context;
static struct {C3D_FrameBuf frameBuf;} render_target, *target = &render_target;
static struct native_render_camera current_camera;
static C3D_Mtx current_projection, current_model_projection;
static int bottom_selected, eye_right, world_low_active;
static int frame_active, midpoint_submitted = 1, inFrame, inSafeTransfer;
static int owned, closing, lose_at_flush, fail_retirement, fail_depth, queued_depth;
static int submitted, dma_live, invalidates, image_saves, camera_saves, commits, frees, allocs;
static u64 clock_tick;
static u32 test_pixels[240*400], source_color, source_depth;

static int aptShouldClose(void) {return closing;}
static int gspHasGpuRight(void) {return owned;}
static u64 svcGetSystemTick(void) {return clock_tick;}
static void svcSleepThread(int ns) {assert(ns == 250000); ++clock_tick;}
static void n3ds_log(const char *path) {(void)path;}
static void *linearAlloc(unsigned int bytes)
{
    assert(bytes == sizeof(test_pixels));
    ++allocs;
    return test_pixels;
}
static void linearFree(void *pixels)
{
    assert(pixels == test_pixels && !dma_live);
    ++frees;
}
static int GSPGPU_FlushDataCache(void *pixels, unsigned int bytes)
{
    assert(pixels == test_pixels && bytes == sizeof(test_pixels) && !dma_live);
    if (lose_at_flush) owned = 0;
    return 0;
}
static int GSPGPU_InvalidateDataCache(void *pixels, unsigned int bytes)
{
    assert(pixels == test_pixels && bytes == sizeof(test_pixels) && !dma_live);
    ++invalidates;
    return 0;
}
static u32 C3D_CalcDepthBufSize(u32 width, u32 height, u32 format)
{
    assert(format == GPU_RB_DEPTH24_STENCIL8);
    return width * height * 4;
}
static int loading_write_capture(const char *path, const void *pixels, unsigned int bytes)
{
    assert(!dma_live);
    if (strstr(path, ".camera")) {
        const u32 *words = pixels;
        assert(bytes == 260 && !memcmp(words, "H2DSCAM1", 8));
        assert(words[2] == 30 && words[4] == 240 && words[5] == 400);
        assert(words[6] == GPU_RB_DEPTH24_STENCIL8 && words[7] == 0);
        assert(words[8] == 0x3f000000); /* presentation time = .5 */
        assert(words[9] == 0x80000000 && words[21] == 0x3f800000); /* actual cameras */
        assert(words[33] == 0x40000000 && words[49] == 0x40400000); /* matrices */
        ++camera_saves;
    } else if (strstr(path, "-complete.tmp")) {
        assert(image_saves == 4 && camera_saves == 2 && strstr(pixels, "d24s8_bytes"));
    } else {
        assert(pixels == test_pixels && bytes == sizeof(test_pixels));
        assert(strstr(path, queued_depth ? ".d24s8" : ".rgba"));
        ++image_saves;
    }
    return 0;
}
static int fixture_rename(const char *from, const char *to)
{
    assert(strstr(from, "-complete.tmp") && strstr(to, "-complete.json"));
    ++commits;
    return 0;
}
static int fixture_remove(const char *path) {(void)path; return 0;}
#define rename fixture_rename
#define remove fixture_remove
#define GX_BUFFER_DIM(w,h) ((w)*(h))
#define GX_TRANSFER_FLIP_VERT(x) (x)
#define GX_TRANSFER_OUT_TILED(x) (x)
#define GX_TRANSFER_RAW_COPY(x) (x)
#define GX_TRANSFER_FMT_RGBA8 0
#define GX_TRANSFER_IN_FORMAT(x) (x)
#define GX_TRANSFER_OUT_FORMAT(x) (x)
#define GX_TRANSFER_SCALE_NO 0
#define GX_TRANSFER_SCALING(x) (x)
#define R_FAILED(x) ((x)<0)
static C3D_Context *C3Di_GetContext(void) {return &context;}
static void gxCmdQueueClear(gxCmdQueue_s *queue) {queue->numEntries = 0;}
static int C3Di_WaitAndClearQueue(int timeout)
{
    assert(timeout == 0);
    if (dma_live && fail_retirement) return 0;
    if (dma_live) {
        dma_live = 0;
        inSafeTransfer = 0; /* Queue callback retired. */
    }
    gxCmdQueueClear(&context.gxQueue);
    return 1;
}
static void halo_c3d_run_queue(gxCmdQueue_s *queue)
{
    assert(owned && !closing && inSafeTransfer && queue->numEntries == 1 && !dma_live);
    ++submitted;
    dma_live = 1;
}
static Result GX_DisplayTransfer(u32 *input, u32 input_dim, u32 *output, u32 output_dim, u32 flags)
{
    assert(input == &source_color && output == test_pixels && !flags);
    assert(input_dim == GX_BUFFER_DIM(240,400) && output_dim == input_dim);
    queued_depth = 0;
    ++context.gxQueue.numEntries;
    return 0;
}
static Result GX_TextureCopy(u32 *input, u32 input_dim, u32 *output, u32 output_dim, u32 bytes, u32 flags)
{
    assert(input == &source_depth && output == test_pixels);
    assert(!input_dim && !output_dim && flags == 8 && bytes == sizeof(test_pixels));
    if (fail_depth) return -1;
    queued_depth = 1;
    ++context.gxQueue.numEntries;
    return 0;
}
'''

CAPTURE_CHECKS = r'''
static void reset(void)
{
    submitted = dma_live = invalidates = image_saves = camera_saves = commits = frees = allocs = 0;
    lose_at_flush = closing = fail_retirement = fail_depth = inFrame = inSafeTransfer = 0;
    owned = 1;
    clock_tick = 123;
    render_target.frameBuf = (C3D_FrameBuf){&source_color,&source_depth,240,400,0,3,0};
}
static int image(int midpoint)
{
    struct native_render_camera culling = {.position={-0.f}};
    current_camera.position[0] = 1.f;
    current_projection.m[0] = 2.f;
    current_model_projection.m[0] = 3.f;
    frame_active = 1;
    n3ds_gpu_capture_midpoint_view(30, midpoint, &culling, .5f);
    frame_active = 0;
    current_camera.position[0] = 99.f;
    current_projection.m[0] = 99.f;
    return n3ds_gpu_capture_midpoint_image(30, midpoint);
}
int main(void)
{
    reset();
    assert(image(1) == 1 && !commits);
    assert(image(0) == 1);
    assert(submitted == 4 && invalidates == 4 && image_saves == 4 && camera_saves == 2 &&
           commits == 1 && frees == 2 && !dma_live);

    reset();
    fail_retirement = 1;
    assert(image(1) == -1);
    assert(submitted == 1 && !invalidates && !image_saves && !frees && dma_live);

    reset();
    lose_at_flush = 1;
    assert(image(1) == -1);
    assert(!submitted && !invalidates && !image_saves && frees == 1);

    reset();
    closing = 1;
    assert(image(1) == -1);
    assert(!submitted && !allocs && !frees);

    reset();
    assert(image(1) == 1);
    fail_depth = 1;
    assert(image(0) == -1);
    assert(submitted == 3 && image_saves == 3 && camera_saves == 1 && !commits && frees == 2);
    return 0;
}
'''

PREFETCH_PLATFORM = r'''
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "engine_texture_descriptor.h"
typedef unsigned char byte;
struct bitmap_data {int type, format; void *hardware_format; unsigned int pixels_offset;};
struct bitmap_plan {
    struct native_texture_descriptor native;
    unsigned int target_width, target_height, source_offset, resolution, lightmap, level, format, width, height;
    unsigned int detail_levels;
    int detail_rgb8;
};
struct native_cube_layout {unsigned int target, level, bytes, stride, skip;};
enum texture_lane {TEXTURE_QUEUED, TEXTURE_PREFETCH};
enum {
    NATIVE_TEXTURE_READ_RANGES = 6, NATIVE_TEXTURE_READ_LIMIT = 1024*1024,
    TEXTURE_STAGE_LIMIT = 64*1024, TEXTURE_QUEUE_STAGE_LIMIT = 256*1024,
    TEXTURE_CUBE_STAGE_LIMIT = 6*64*64*4, TEXTURE_ARCHIVE_RAW = 1, TEXTURE_ARCHIVE_KIND_CANDIDATE = 2
};
struct texture_archive_selection {unsigned int format, codec, bytes, kind, offset, hash;};
struct native_texture_read_range {unsigned int offset, bytes; void *destination;};
static const unsigned int scratch_bytes = 516 + 2048, output_bytes = 4096;
static unsigned int live, peak;
static int fail_reserve, abandoned;
static unsigned int image[4096/4];
static struct texture_prefetch_job {
    const struct bitmap_data *bitmap;
    struct bitmap_plan plan;
    struct native_cube_layout cube;
    int cube_format;
    byte *source;
    void *resource;
    unsigned int *images[6];
    unsigned short *distance;
    unsigned int generation, archive_hash, pending_slot;
    enum texture_lane lane;
    int archived;
} texture_job;

#define TEXTURE_PROFILE(statement) ((void)0)
#define MIN(a,b) ((a)<(b)?(a):(b))
#define texture_archive_select(...) 0
#define texture_data_path(...) 0
static unsigned int texture_resolution = 32;
static int owns_bitmap(const struct bitmap_data *bitmap) {return bitmap != NULL;}
static int cube_layout(const struct bitmap_data *bitmap, unsigned int resolution, struct native_cube_layout *cube)
{
    (void)bitmap;
    (void)resolution;
    (void)cube;
    return 0;
}
static int bitmap_plan(const struct bitmap_data *bitmap, struct bitmap_plan *plan)
{
    (void)bitmap;
    plan->native.source_bytes = 513;
    plan->native.resident_bytes = output_bytes;
    plan->lightmap = 1;
    return 1;
}
static int texture_can_stage(unsigned int bytes) {return bytes == output_bytes;}
static int n3ds_engine_cache_read_path(unsigned int offset, unsigned int bytes, char *path)
{
    (void)offset;
    (void)bytes;
    strcpy(path, "sdmc:/fixture");
    return 1;
}
static void n3ds_algorithm_profile_staging_acquire(unsigned int bytes)
{
    live += bytes;
    if (live > peak) peak = live;
}
static void n3ds_algorithm_profile_staging_release(unsigned int bytes)
{
    assert(live >= bytes);
    live -= bytes;
}
static void *n3ds_gpu_texture_reserve_descriptor(const struct native_texture_descriptor *descriptor,
    struct native_texture_span images[6])
{
    assert(live == scratch_bytes && descriptor->scratch_bytes == scratch_bytes);
    memset(images, 0, 6*sizeof(*images));
    images[0].data = image;
    return fail_reserve ? NULL : image;
}
static void n3ds_gpu_texture_abandon(void *resource)
{
    assert(resource == image);
    ++abandoned;
}
static unsigned int n3ds_engine_cache_generation(void) {return 1;}
static int texture_prepare_pixels(void *context) {(void)context; return 1;}
static int n3ds_texture_reader_submit_ranges(const char *path, const struct native_texture_read_range *ranges,
    unsigned int count, int (*prepare)(void *), void *context)
{
    assert(live == scratch_bytes + output_bytes && path && ranges && count == 1 && prepare && context);
    return 0;
}
'''

PREFETCH_CHECKS = r'''
int main(void)
{
    struct bitmap_data bitmap = {0};
    fail_reserve = 1;
    assert(!texture_prefetch_submit(&bitmap, TEXTURE_PREFETCH, 0));
    assert(!live && peak == scratch_bytes && !abandoned);

    fail_reserve = 0;
    peak = 0;
    assert(!texture_prefetch_submit(&bitmap, TEXTURE_PREFETCH, 0));
    assert(!live && peak == scratch_bytes + output_bytes && abandoned == 1);
    return 0;
}
'''


QUEUE_CALLBACK_PLATFORM = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
typedef uint64_t u64;
typedef int64_t s64;
typedef struct {int unused;} gxCmdQueue_s;
static gxCmdQueue_s queue;
static unsigned int queue_callback_pending;
static int inFrame, sleeps, release_after, queue_waits;
static bool queue_ready=true;
static u64 ticks;
static bool gxCmdQueueWait(gxCmdQueue_s *q,s64 timeout)
{assert(q==&queue); (void)timeout; ++queue_waits; return queue_ready;}
static u64 svcGetSystemTick(void) {return ticks;}
static void svcSleepThread(s64 ns)
{
    assert(ns==10000); ticks+=2;
    if(++sleeps==release_after)queue_callback_pending=0;
}
static bool C3Di_WaitAndClearQueue(s64 timeout);
'''

QUEUE_CALLBACK_CHECKS = r'''
static bool C3Di_WaitAndClearQueue(s64 timeout)
{return halo_c3d_wait_queue(&queue,timeout);}
int main(void)
{
    queue_callback_pending=1; release_after=3;
    assert(!halo_c3d_wait_queue(&queue,0) && !sleeps);
    assert(n3ds_c3d_texture_mirror_wait_idle() && sleeps==3);
    queue_callback_pending=1; sleeps=0; release_after=20;
    assert(!halo_c3d_wait_queue(&queue,10) && sleeps==5);
    sleeps=0; release_after=3;
    assert(halo_c3d_wait_queue(&queue,20) && sleeps==3);
    queue_ready=false; sleeps=0;
    assert(!halo_c3d_wait_queue(&queue,-1) && !sleeps);
    inFrame=1; int before=queue_waits;
    assert(!n3ds_c3d_texture_mirror_wait_idle() && before==queue_waits);
    return 0;
}
'''

MIRROR_COPY_PLATFORM = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
typedef int Result;
typedef struct {unsigned int numEntries;} gxCmdQueue_s;
typedef struct {gxCmdQueue_s gxQueue;} C3D_Context;
static C3D_Context context;
static int inFrame,inSafeTransfer,closing,owned=1,enqueue_fail,extra_entry;
static int dma_live,submitted,waits,callback_retired;
static unsigned int source,destination;
#define R_FAILED(r) ((r)<0)
static int aptShouldClose(void) {return closing;}
static int gspHasGpuRight(void) {return owned;}
static C3D_Context *C3Di_GetContext(void) {return &context;}
static void gxCmdQueueClear(gxCmdQueue_s *q) {q->numEntries=0;}
static int C3Di_WaitAndClearQueue(int timeout)
{
    assert(timeout==-1); ++waits;
    if(dma_live) {dma_live=0; inSafeTransfer=0; callback_retired=1;}
    gxCmdQueueClear(&context.gxQueue); return 1;
}
static Result GX_TextureCopy(void *src,unsigned int gap0,void *dst,unsigned int gap1,unsigned int bytes,unsigned int flags)
{
    assert(src==&source && dst==&destination && !gap0 && !gap1 && bytes==16 && flags==8);
    if(enqueue_fail)return -1;
    context.gxQueue.numEntries=1+extra_entry;return 0;
}
static void halo_c3d_run_queue(gxCmdQueue_s *q)
{
    assert(q==&context.gxQueue && q->numEntries==1 && inSafeTransfer && !dma_live);
    dma_live=1; ++submitted;
}
'''

MIRROR_COPY_CHECKS = r'''
int main(void)
{
    assert(n3ds_c3d_texture_mirror_copy(&source,&destination,16));
    assert(submitted==1 && waits==2 && callback_retired && !dma_live && !inSafeTransfer);
    enqueue_fail=1;
    assert(!n3ds_c3d_texture_mirror_copy(&source,&destination,16) && submitted==1);
    enqueue_fail=0; extra_entry=1;
    assert(!n3ds_c3d_texture_mirror_copy(&source,&destination,16) && submitted==1 && !context.gxQueue.numEntries);
    extra_entry=0; owned=0; int before=waits;
    assert(!n3ds_c3d_texture_mirror_copy(&source,&destination,16) && waits==before);
    owned=1; closing=1;
    assert(!n3ds_c3d_texture_mirror_copy(&source,&destination,16) && waits==before);
    closing=0; inFrame=1;
    assert(!n3ds_c3d_texture_mirror_copy(&source,&destination,16) && waits==before);
    inFrame=0; inSafeTransfer=1;
    assert(!n3ds_c3d_texture_mirror_copy(&source,&destination,16) && waits==before);
    return 0;
}
'''


def extract_function(path, declaration):
    """Find the definition and its column-zero closing brace."""
    text = path.read_text()
    start = text.index(f'\n{declaration}') + 1
    return text[start:text.index('\n}', start) + 2] + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='cc', help='host compiler with UBSan support')
    args = parser.parse_args()
    adapter = PORT / 'src/engine_citro3d_frame.c'
    renderer = (PORT / 'src/engine_gpu_renderer.c').read_text()
    capture_start = renderer.index('enum { CAPTURE_WIDTH=')
    capture_body = renderer[capture_start:renderer.index('\n#endif', capture_start)] + '\n'
    fixtures = (
        ('mirror-copy', MIRROR_COPY_PLATFORM
         + extract_function(adapter, 'int n3ds_c3d_texture_mirror_copy(')
         + MIRROR_COPY_CHECKS),
        ('queue-callback', QUEUE_CALLBACK_PLATFORM
         + extract_function(adapter, 'static bool halo_c3d_wait_queue(gxCmdQueue_s *queue, s64 timeout_ticks)')
         + extract_function(adapter, 'int n3ds_c3d_texture_mirror_wait_idle(')
         + QUEUE_CALLBACK_CHECKS),
        ('capture', CAPTURE_PLATFORM
         + extract_function(adapter, 'int n3ds_c3d_capture_wait_idle(')
         + extract_function(adapter, 'int n3ds_c3d_capture_transfer(')
         + capture_body + CAPTURE_CHECKS),
        ('prefetch', PREFETCH_PLATFORM
         + extract_function(PORT / 'include/engine_texture_prefetch.inl', 'static int texture_prefetch_submit(')
         + PREFETCH_CHECKS),
    )
    with tempfile.TemporaryDirectory(prefix='halo-capture-staging-') as directory:
        for name, body in fixtures:
            source = Path(directory) / f'{name}.c'
            executable = Path(directory) / name
            source.write_text(body)
            subprocess.run([
                args.cc, '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-fsanitize=undefined', '-fno-sanitize-recover=undefined',
                '-I', str(PORT / 'include'), str(source), '-o', str(executable),
            ], check=True, timeout=45)
            subprocess.run([str(executable)], check=True, timeout=10)
            print(f'{name} ownership checks passed')


if __name__ == '__main__':
    main()
