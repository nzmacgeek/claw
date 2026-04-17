#include "devev.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

/*
 * Device event implementation for BlueyOS kernel interface.
 *
 * The kernel exposes device events through a simple character device.
 * Each event is encoded as a fixed-size record that can be read non-blockingly.
 *
 * Event format (from kernel):
 *   struct {
 *       uint32_t type;      // event type code
 *       uint32_t timestamp; // kernel monotonic time
 *   };
 */

#define DEVEV_RECORD_SIZE 8

int devev_open(const char *path) {
    if (!path || !*path) {
        log_debug("devev", "No device event path specified");
        return -1;
    }

    int fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        if (errno != ENOENT)
            log_debug("devev", "Cannot open %s: %s", path, strerror(errno));
        return -1;
    }

    log_info("devev", "Opened device event interface: %s (fd=%d)", path, fd);
    return fd;
}

void devev_close(int fd) {
    if (fd >= 0) {
        close(fd);
        log_debug("devev", "Closed device event fd %d", fd);
    }
}

int devev_poll(int fd, devev_t *ev) {
    if (fd < 0 || !ev) {
        return -1;
    }

    uint8_t buf[DEVEV_RECORD_SIZE];
    ssize_t n = read(fd, buf, sizeof(buf));

    if (n == 0) {
        /* EOF — device removed or closed */
        log_warning("devev", "EOF on event fd — device may have been removed");
        return -1;
    }

    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            /* No data available (non-blocking) */
            return 0;
        }
        if (errno == EINTR) {
            /* Interrupted by signal — retry later */
            return 0;
        }
        log_warning("devev", "read() error on event fd: %s", strerror(errno));
        return -1;
    }

    if (n != DEVEV_RECORD_SIZE) {
        log_warning("devev", "Partial event read: got %zd bytes, expected %d",
                    n, DEVEV_RECORD_SIZE);
        return -1;
    }

    /* Decode event record (little-endian assumed) */
    uint32_t type = (uint32_t)buf[0]
                  | ((uint32_t)buf[1] << 8)
                  | ((uint32_t)buf[2] << 16)
                  | ((uint32_t)buf[3] << 24);
    uint32_t timestamp = (uint32_t)buf[4]
                       | ((uint32_t)buf[5] << 8)
                       | ((uint32_t)buf[6] << 16)
                       | ((uint32_t)buf[7] << 24);

    ev->type = (devev_type_t)type;
    ev->timestamp = timestamp;

    log_debug("devev", "Received event: type=%u timestamp=%u", type, timestamp);
    return 1;
}

const char *devev_type_name(devev_type_t type) {
    switch (type) {
        case DEV_EV_NONE:         return "none";
        case DEV_EV_CTRL_ALT_DEL: return "ctrl-alt-del";
        case DEV_EV_POWER_BUTTON: return "power-button";
        case DEV_EV_SLEEP_BUTTON: return "sleep-button";
        default:                  return "unknown";
    }
}
