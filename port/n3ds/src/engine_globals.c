/* Typed storage for original linker COMMON globals absent from reconstructed C.
 * Initialization and ownership remain in the original engine routines. */
#include "cseries.h"
#include "data.h"
#include "cluster_partitions.h"
struct data_array *light_data;
struct cluster_partition light_cluster_partition;
struct data_array *hs_thread_data;
struct data_array *hs_global_data;
