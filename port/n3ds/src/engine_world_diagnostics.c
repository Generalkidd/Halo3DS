/* Focused startup diagnostics; wrappers retain the original allocator behavior. */
#include "cseries.h"
#include "data.h"
void n3ds_log(const char *message);
long __real_datum_new(struct data_array *data);
void __real_data_iterator_new(struct data_iterator *iterator,struct data_array *data);
void __real_data_verify(struct data_array *data);
static void missing_pool(const char *operation,void *caller)
{
    char message[128];
    snprintf(message,sizeof(message),"WORLD MISSING POOL: %s caller=%p",operation,caller);
    n3ds_log(message);
}
long __wrap_datum_new(struct data_array *data)
{
    if(!data) missing_pool("datum_new",__builtin_return_address(0));
    return __real_datum_new(data);
}
void __wrap_data_iterator_new(struct data_iterator *iterator,struct data_array *data)
{
    if(!data) missing_pool("data_iterator_new",__builtin_return_address(0));
    else if(!data->valid) {
        char message[160];snprintf(message,sizeof(message),"WORLD INVALID POOL: %.32s caller=%p",data->name,__builtin_return_address(0));n3ds_log(message);
    }
    __real_data_iterator_new(iterator,data);
}
void __wrap_data_verify(struct data_array *data)
{
    if(!data) missing_pool("data_verify",__builtin_return_address(0));
    __real_data_verify(data);
}
