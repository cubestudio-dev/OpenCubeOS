/* Host test stub: crypto.c references kernel PMM for scratch frames.
 * On host we just malloc/free instead. NOT kernel code. */
#include "types.h"
#include <stdlib.h>
void *pmm_alloc_frame(void) { return malloc(4096); }
void  pmm_free_frame(void *p) { free(p); }
u64   oc_timer_ticks(void) { return 0; }
