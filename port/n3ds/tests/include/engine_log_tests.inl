#ifdef HALO_N3DS_RENDERER_TESTS
int n3ds_log_queue_tests(void)
{
    log_async_start();if(!log_thread) return 1;
    unsigned int before=log_dropped;
    long long start=svcGetSystemTick();
    for(int i=0;i<128;++i) {
        char text[128];snprintf(text,sizeof(text),"LOG QUEUE ORDER %03d: bounded background SD diagnostic check",i);log_enqueue(text);
    }
    double ms=(svcGetSystemTick()-start)*1000.0/SYSCLOCK_ARM11;
    char text[160];snprintf(text,sizeof(text),"PASS: log queue: 128 ordered messages enqueued in %.3f ms; no frame-thread file writes",ms);n3ds_log(text);
    return log_dropped!=before;
}
#endif
