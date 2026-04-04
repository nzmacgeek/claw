#ifndef __CLAW_TIME_H__
#define __CLAW_TIME_H__

#include <time.h>
#include <stdint.h>

/* Get current monotonic time (for measuring durations) */
uint64_t time_monotonic_ms(void);

/* Get current real time (for timestamps) */
time_t time_now(void);

/* Format time as ISO-8601 string */
char *time_format_iso8601(time_t t);

/* Format time as human-readable string */
char *time_format_readable(time_t t);

/* Calculate milliseconds until deadline */
int64_t time_until_ms(uint64_t deadline_ms);

/* Sleep for milliseconds */
void time_sleep_ms(uint32_t ms);

/* Create timeout deadline (ms from now) */
uint64_t time_deadline_in_ms(uint32_t ms);

#endif /* __CLAW_TIME_H__ */
