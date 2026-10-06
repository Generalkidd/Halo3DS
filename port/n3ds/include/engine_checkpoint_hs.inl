/* Included in hs_runtime.c so private VM fields keep their real C types. */
#include "engine_checkpoint.h"
int n3ds_checkpoint_hs_visit(n3ds_checkpoint_visitor visit)
{
    long index;
    for(index=data_next_index(hs_global_data,NONE);index!=NONE;index=data_next_index(hs_global_data,index)) {
        int absolute=index&65535;
        short designator=absolute<hs_external_global_count?(short)(absolute|0x8000):(short)(absolute-hs_external_global_count);
        struct hs_global_datum *g=datum_get(hs_global_data,index);
        if(hs_global_get_type(designator)==_hs_type_string && !visit(&g->value.string,0)) return 0;
    }
    for(index=data_next_index(hs_thread_data,NONE);index!=NONE;index=data_next_index(hs_thread_data,index)) {
        struct hs_thread_datum *t=hs_thread_get(index);
        struct hs_stack_frame *base=(void *)t->stack_data,*f=t->stack;
        unsigned int guard=0;
        if(!visit(&t->stack,0)) return 0;
        while(f) {
            if(++guard>32 || (byte *)f<t->stack_data || (byte *)(f+1)>t->stack_data+HS_THREAD_STACK_SIZE) return 0;
            if(!visit(&f->previous,0) || !visit(&f->result,f==t->stack)) return 0;
            /* A suspended child's result has not been produced yet. Its
             * parent's result address is still needed when the child returns.
             * Finished/base frames retain no meaningful result address. */
            if(f!=base && f->expression_index!=NONE) {
                struct hs_syntax_node *x=hs_syntax_get(f->expression_index);
                byte *data=(byte *)(((unsigned long)f->data+3)&~3UL);
                if(TEST_FLAG(x->flags,_hs_syntax_node_script_bit)) {
                    struct hs_script *script=TAG_BLOCK_GET_ELEMENT(&global_scenario_get()->hs_scripts,x->index,struct hs_script);
                    if(script->return_type==_hs_type_string && data+4<=f->data+f->size && !visit(data,f!=t->stack)) return 0;
                } else {
                    const struct hs_function_definition *fn=hs_function_get(x->index);
                    /* Special forms have their own typed scratch layouts. A
                     * pending child's return slot is overwritten on return. */
                    int result_offset=-1;
                    if(x->type==_hs_type_string) {
                        if(fn->evaluate==hs_evaluate_begin) result_offset=4;
                        else if(fn->evaluate==hs_evaluate_if || fn->evaluate==hs_evaluate_begin_random) result_offset=8;
                    }
                    if(fn->evaluate==hs_evaluate_inspect && hs_syntax_get(hs_syntax_get(x->data)->next_node_index)->type==_hs_type_string) result_offset=0;
                    if(result_offset>=0 && data+result_offset+4<=f->data+f->size && !visit(data+result_offset,f!=t->stack)) return 0;
                    if(fn->evaluate==hs_evaluate_debug_string && data+8+MAXIMUM_HS_DEBUG_STRING_ARGUMENTS*4<=f->data+f->size) {
                        long count=*(long *)(data+4);
                        if(count<0 || count>MAXIMUM_HS_DEBUG_STRING_ARGUMENTS) return 0;
                        for(int j=0;j<count;++j) if(!visit(data+8+j*4,0)) return 0;
                    }
                    int count=fn->parameter_count;
                    short argument_type=NONE;
                    if(fn->evaluate==hs_evaluate_equality) {
                        count=2;
                        argument_type=hs_syntax_get(hs_syntax_get(x->data)->next_node_index)->type;
                    }
                    /* Generic macro arguments are a typed long[] followed by
                     * the evaluation cursor. Ignore not-yet-evaluated slots. */
                    if(result_offset<0 && fn->evaluate!=hs_evaluate_debug_string && count>0 && count<=64 && data+count*4+2<=f->data+f->size) {
                        int evaluated=*(short *)(data+count*4);
                        if(evaluated>=0 && evaluated<=count) for(int j=0;j<evaluated;++j) {
                            short type=argument_type!=NONE?argument_type:fn->parameter_types[j];
                            if(type==_hs_type_string && !visit(data+j*4,f!=t->stack && j==evaluated-1)) return 0;
                        }
                    }
                }
            }
            if(f==base) break;
            if(!f->previous || f->previous>=f) return 0;
            f=f->previous;
        }
    }
    return 1;
}
