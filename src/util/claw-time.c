#include "claw-time.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>

uint64_t time_monotonic_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

time_t time_now(void) {
    return time(NULL);
}

char *time_format_iso8601(time_t t) {
    struct tm *tm_info = localtime(&t);
    char *result = malloc(32);
    if (result) {
        strftime(result, 32, "%Y-%m-%dT%H:%M:%SZ", tm_info);
    }
    return result;
}

char *time_format_readable(time_t t) {
    struct tm *tm_info = localtime(&t);
    char *result = malloc(64);
    if (result) {
        strftime(result, 64, "%Y-%m-%d %H:%M:%S", tm_info);
    }
    return result;
}

int64_t time_until_ms(uint64_t deadline_ms) {
    uint64_t now = time_monotonic_ms();
    if (now >= deadline_ms) {
        return 0;
    }
    return (int64_t)(deadline_ms - now);
}

void time_sleep_ms(uint32_t ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000;
    nanosleep(&ts, NULL);
}

uint64_t time_deadline_in_ms(uint32_t ms) {
    return time_monotonic_ms() + ms;
}
