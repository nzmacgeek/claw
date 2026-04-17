#ifndef __DEVEV_H__
#define __DEVEV_H__

/*
 * devev.h — Device event interface for kernel-originated events.
 *
 * BlueyOS kernel communicates certain hardware/keyboard events to PID 1
 * through a character device interface. This module provides polling and
 * decoding of those events.
 */

#include <stdint.h>

/* Device event types (from BlueyOS kernel) */
typedef enum {
    DEV_EV_NONE            = 0,
    DEV_EV_CTRL_ALT_DEL    = 1,    /* Ctrl+Alt+Delete pressed */
    DEV_EV_POWER_BUTTON    = 2,    /* Power button pressed */
    DEV_EV_SLEEP_BUTTON    = 3,    /* Sleep button pressed */
} devev_type_t;

/* Device event structure */
typedef struct {
    devev_type_t type;
    uint32_t     timestamp;  /* Kernel monotonic time */
} devev_t;

/*
 * Open the device event file descriptor.
 * Path is typically "/dev/claw-events" or "/dev/input/event0"
 * Returns fd >= 0 on success, -1 on error.
 */
int devev_open(const char *path);

/*
 * Close the device event file descriptor.
 */
void devev_close(int fd);

/*
 * Poll for a device event (non-blocking).
 * Returns 1 and fills *ev if an event is available,
 *         0 if no event is pending,
 *        -1 on error.
 */
int devev_poll(int fd, devev_t *ev);

/*
 * Get human-readable name for event type.
 */
const char *devev_type_name(devev_type_t type);

#endif /* __DEVEV_H__ */
