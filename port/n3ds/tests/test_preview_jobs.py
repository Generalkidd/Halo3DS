"""Exercise production preview scheduling with delayed worker/GPU stand-ins."""
from pathlib import Path
import subprocess,sys,tempfile
ROOT=Path(__file__).resolve().parents[3]
s=(ROOT/'port/n3ds/preview/engine_pc_preview.c').read_text()
identity=s[s.index('enum { PREVIEW_EMPTY'):s.index('static unsigned int triangle_limit')]
scheduler=s[s.index('/* Kevin Boggs: n3ds_preview_update'):s.index('void n3ds_pc_preview_input')]
close=s[s.index('void n3ds_pc_preview_close'):s.index('/* Kevin Boggs: n3ds_preview_dispose')]
available=s[s.index('int n3ds_preview_available(void)'):]
thumbnail=s[s.index('void *n3ds_pc_preview_thumbnail(int map)'):s.index('/* Kevin Boggs: preview_worker')]
prefix=r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define PREVIEW_TRIANGLES 21840
#define PREVIEW_TEXTURE_SIZE 64
static int selected_map,selected_page,selected_field,selected_weapon,selected_vehicle;
static int dragging,touch_held,touch_owner;
static unsigned int triangle_limit,triangles,batch_count;
static void *thumbnail_pixels,*thumbnail_texture;
#define THUMBNAIL_SIZE 128
struct vertex {float values[8];};static struct vertex *vertices;
static unsigned short *indices;
struct preview_batch {void *texture,*pixels;};static struct preview_batch batches[4];
static float yaw,elevation,zoom;
static unsigned int token_live,thread_done,creates,closes,uploads,flushes,releases;
static int fail_thread,fail_mesh;
'''
stubs=r'''
static unsigned int preview_worker(void *p){(void)p;return 1;}
static unsigned int n3ds_thread_create(unsigned int flags,unsigned int(*f)(void*),void *p){
 assert(flags==2 && f==preview_worker && !p && !token_live);++creates;
 thread_done=0;return token_live=fail_thread?0:creates;
}
static int n3ds_thread_result(unsigned int token,unsigned int *result){
 assert(token==token_live);*result=1;return thread_done;
}
static void n3ds_thread_close(unsigned int token){assert(token==token_live);token_live=0;++closes;}
static int n3ds_input_platform_original_model(void){return 1;}
static void preview_release(void){
 assert(!token_live);free(vertices);free(indices);vertices=NULL;indices=NULL;
 free(thumbnail_pixels);thumbnail_pixels=NULL;thumbnail_texture=NULL;
 for(unsigned i=0;i<batch_count;++i)free(batches[i].pixels);
 memset(batches,0,sizeof(batches));triangles=batch_count=0;state_loaded=PREVIEW_EMPTY;++releases;
}
static int allocate_mesh(void){
 assert(triangle_limit==8192 || triangle_limit==PREVIEW_TRIANGLES);if(fail_mesh)return 0;
 vertices=calloc(3,sizeof(*vertices));indices=calloc(3,sizeof(*indices));return 1;
}
static void n3ds_gpu_geometry_flush(void *p,unsigned int n){assert(!token_live && p && n && n<=65536);++flushes;}
static void *n3ds_gpu_texture_create(void *p,int w,int h){assert(!token_live && p && w==h && (w==64 || w==128));++uploads;return (void*)1;}
static void finish_worker(void){
 triangles=1;batch_count=2;batches[0].pixels=malloc(16);batches[1].pixels=malloc(16);thread_done=1;
}
'''
test=r'''
int main(void){
 requested_xbox=1;preview_request(1,0,0,0,0);assert(n3ds_preview_loading());
 n3ds_preview_update();assert(creates==1 && token_live && !n3ds_preview_available());
 for(int i=0;i<20;++i)n3ds_preview_update();assert(creates==1 && !uploads && !flushes);
 /* Rapid scrolling keeps one worker and the newest request only. */
 preview_request(18,0,0,0,0);preview_request(16,0,0,0,0);
 n3ds_preview_update();assert(creates==1 && preview_cancelled(NULL));
 finish_worker();n3ds_preview_update();assert(creates==2 && selected_map==16 && !uploads);
 finish_worker();n3ds_preview_update();assert(flushes==1 && !uploads);
 n3ds_preview_update();assert(flushes==2 && !uploads);
 n3ds_preview_update();assert(uploads==1 && !n3ds_preview_available());
 n3ds_preview_update();assert(uploads==2 && !n3ds_preview_available());
 n3ds_preview_update();assert(n3ds_preview_available() && !n3ds_preview_loading());
 /* Closing cannot free GPU-visible data until next completed frame. */
 unsigned before=releases;n3ds_pc_preview_close();assert(releases==before && !n3ds_preview_available());
 n3ds_preview_update();assert(releases==before+1);
 /* Close during a read neither waits nor publishes the obsolete preview. */
 preview_request(1,0,0,0,0);n3ds_preview_update();n3ds_pc_preview_close();
 n3ds_preview_update();assert(token_live);finish_worker();n3ds_preview_update();
 assert(!worker && !wanted.valid && !n3ds_preview_available());
 /* Unavailable worker or memory must not trigger a synchronous fallback/retry loop. */
 fail_thread=1;preview_request(1,0,0,0,0);n3ds_preview_update();before=creates;
 for(int i=0;i<20;++i)n3ds_preview_update();assert(creates==before && !n3ds_preview_loading());
 n3ds_pc_preview_close();n3ds_preview_update();fail_thread=0;fail_mesh=1;
 preview_request(16,0,0,0,0);n3ds_preview_update();assert(!n3ds_preview_loading());
 n3ds_pc_preview_close();n3ds_preview_update();
 /* A thumbnail publishes before the mesh, but never for a stale selection. */
 fail_mesh=0;requested_xbox=0;preview_request(6,0,0,0,0);n3ds_preview_update();
 assert(!n3ds_pc_preview_thumbnail(6));thumbnail_pixels=malloc(65536);finish_worker();
 before=uploads;n3ds_preview_update();assert(uploads==before+1 && n3ds_pc_preview_thumbnail(6));
 assert(!n3ds_preview_available() && !n3ds_pc_preview_thumbnail(7));
 preview_request(7,0,0,0,0);assert(!n3ds_pc_preview_thumbnail(6));
 n3ds_preview_update();assert(!thumbnail_texture && token_live);
 thumbnail_pixels=malloc(65536);finish_worker();n3ds_pc_preview_close();
 before=uploads;n3ds_preview_update();assert(uploads==before && !thumbnail_pixels && !thumbnail_texture);
 puts("PASS: preview cancellation, latest-only publication, bounded uploads, deferred cleanup and failure fallback");
}
'''
command=sys.argv[1:]
if command and command[0]=='--':command.pop(0)
if not command:command=['clang']
with tempfile.TemporaryDirectory(prefix='halo-preview-jobs-',ignore_cleanup_errors=True) as tmp:
 p=Path(tmp);src=p/'jobs.c';exe=p/('jobs.exe' if sys.platform=='win32' else 'jobs')
 src.write_text(prefix+identity+stubs+close+scheduler+available+thumbnail+test)
 subprocess.run(command+['-std=c11',str(src),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
