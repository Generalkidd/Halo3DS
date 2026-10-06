#ifdef HALO_N3DS_RENDERER_TESTS
int n3ds_model_parallel_tests(void)
{
    const unsigned int size=4097;
    struct model_vertex_compressed *raw=(calloc)(size,sizeof(*raw));
    struct model_vertex_uncompressed *decoded=(malloc)(size*sizeof(*decoded));
    struct native_render_vertex *actual=(malloc)(size*sizeof(*actual)),*expected=(malloc)(size*sizeof(*expected));
    int ok=0;
    if(!raw || !decoded || !actual || !expected) goto done;
    real_matrix4x3 bones[2]={{0},{0}};real_vector2d uv={1.3f,.7f};
    struct rasterizer_model_skinning skin={0};skin.node_matrices=bones;skin.node_matrix_count=2;
    for(unsigned int b=0;b<2;++b) {bones[b].scale=b?-.75f:1; bones[b].forward.i=bones[b].left.j=bones[b].up.k=1;bones[b].position.x=b*.2f;}
    for(unsigned int i=0;i<size;++i) {
        raw[i].nodes[0]=0;raw[i].nodes[1]=3;raw[i].node_weight=i%32768;
        raw[i].normal=0x3ff;raw[i].position.x=i*.001f;raw[i].position.y=(int)(i%7)-3;
    }
    struct model_decode_range decode={raw,decoded};
    if(!n3ds_parallel_range(decode_vertex_range,&decode,size,2,3)) goto done;
    for(unsigned int i=0;i<size;++i) {
        struct model_vertex_uncompressed reference;
        if(!n3ds_model_decode_vertex(raw+i,&reference) || memcmp(&reference,decoded+i,sizeof(reference))) goto done;
    }
    struct model_skin_range parallel={decoded,&skin,&uv,actual},serial={decoded,&skin,&uv,expected};
    const unsigned int counts[]={1,63,256,1025,4097};
    for(unsigned int test=0;test<5;++test) {
        unsigned int count=counts[test];long long one=0,two=0;
        for(unsigned int repeat=0;repeat<8;++repeat) {
            long long start=n3ds_engine_ticks();if(!skin_vertex_range(&serial,0,count)) goto done;
            one+=n3ds_engine_ticks()-start;
            start=n3ds_engine_ticks();if(!n3ds_parallel_range(skin_vertex_range,&parallel,count,2,3)) goto done;
            two+=n3ds_engine_ticks()-start;
            if(memcmp(actual,expected,count*sizeof(*actual))) goto done;
        }
        char text[160];double scale=1000.0/n3ds_engine_tick_frequency()/8;
        snprintf(text,sizeof(text),"CPU WORKER BENCH: skin_vertices=%u serial_ms=%.3f parallel_ms=%.3f bit_exact=1",count,one*scale,two*scale);n3ds_log(text);
    }
    decoded[size-1].node_weights[0]=-1;
    if(n3ds_parallel_range(skin_vertex_range,&parallel,size,2,3)) goto done;
    n3ds_log("PASS: parallel models: 4097 decoded vertices and 40 pose comparisons bit-exact; worker-range invalid input rejected");ok=1;
done:
    free(raw);free(decoded);free(actual);free(expected);return ok;
}
#endif
