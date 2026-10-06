#include <3ds.h>
#include <citro3d.h>
#include "engine_renderer.h"
#include "texture_test_shader.h"

/* Citro3D dereferences the previously bound shader when switching programs.
 * Keep this parking program alive for the entire C3D context lifetime, so
 * temporary passes may safely destroy their own shader programs. */
static DVLB_s *shader;
static shaderProgram_s parking;
static int ready;
int n3ds_gpu_programs_initialize(void)
{
    shader=DVLB_ParseFile((u32 *)texture_test_shader, sizeof(texture_test_shader));
    if (!shader) return 0;
    shaderProgramInit(&parking);
    if (R_FAILED(shaderProgramSetVsh(&parking, &shader->DVLE[0]))) {
        shaderProgramFree(&parking); DVLB_Free(shader); shader=NULL; return 0;
    }
    ready=1; C3D_BindProgram(&parking); return 1;
}
void n3ds_gpu_program_park(void)
{
    if (!ready) svcBreak(USERBREAK_PANIC);
    C3D_BindProgram(&parking);
}
void n3ds_gpu_programs_dispose(void)
{
    /* Only after C3D_Fini: the context must no longer refer to parking. */
    if (ready) { shaderProgramFree(&parking); DVLB_Free(shader); }
    shader=NULL; ready=0;
}
