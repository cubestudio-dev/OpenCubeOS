/* Host test stub: provide core_rtc_unix_now() without CMOS port I/O. */
#include "types.h"
#include <time.h>
u64 core_rtc_unix_now(void) { return (u64)time(NULL); }
