/* Host test stub: provide rtc_unix_now() without CMOS port I/O. */
#include "types.h"
#include <time.h>
u64 rtc_unix_now(void) { return (u64)time(NULL); }
