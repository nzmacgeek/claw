#ifndef __CLAW_H__
#define __CLAW_H__

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <sys/types.h>

/* Version — generated at configure time from claw-version.h.in */
#include "claw-version.h"
#define CLAW_VERSION_CODE 0x000100
#include "claw-build.h"  /* defines CLAW_BUILD_ID, CLAW_BUILD_DIRTY */

/* Logging levels */
typedef enum {
    LOG_DEBUG = 0,
    LOG_INFO = 1,
    LOG_WARNING = 2,
    LOG_ERROR = 3,
    LOG_CRITICAL = 4
} log_level_t;

/* Unit types */
typedef enum {
    UNIT_SERVICE,
    UNIT_TARGET
} unit_type_t;

/* Service types */
typedef enum {
    SERVICE_SIMPLE,      /* Foreground process */
    SERVICE_FORKING,     /* Daemonizes to background */
    SERVICE_ONESHOT,     /* Runs once and exits */
    SERVICE_NOTIFY,      /* Signal readiness via IPC */
    SERVICE_TIMER        /* Triggered by timer */
} service_type_t;

/* Service states */
typedef enum {
    SERVICE_INACTIVE,
    SERVICE_ACTIVATING,
    SERVICE_ACTIVE,
    SERVICE_DEACTIVATING,
    SERVICE_RESTARTING,
    SERVICE_FAILED,
    SERVICE_DEAD
} service_state_t;

/* Target states */
typedef enum {
    TARGET_INACTIVE,
    TARGET_ACTIVATING,
    TARGET_ACTIVE,
    TARGET_DEACTIVATING,
    TARGET_FAILED
} target_state_t;

/* System states */
typedef enum {
    SYSTEM_INIT,
    SYSTEM_EARLY_BOOT,
    SYSTEM_DEVICES,
    SYSTEM_ROOTFS,
    SYSTEM_BASIC,
    SYSTEM_NETWORK,
    SYSTEM_MULTIUSER,
    SYSTEM_GRAPHICAL,
    SYSTEM_RUNTIME,
    SYSTEM_SHUTDOWN,
    SYSTEM_OFF
} system_state_t;

/* Dependency types */
typedef enum {
    DEP_HARD,           /* Must be satisfied */
    DEP_SOFT,           /* Nice to have, don't fail if missing */
    DEP_ORDER           /* Just ordering, not state-based */
} dep_type_t;

/* Event types */
typedef enum {
    EVENT_LIFECYCLE,
    EVENT_TIMER,
    EVENT_HARDWARE,
    EVENT_FILESYSTEM,
    EVENT_NETWORK,
    EVENT_CUSTOM
} event_type_t;

/* IPC command codes */
typedef enum {
    CLAW_CMD_START = 1,
    CLAW_CMD_STOP = 2,
    CLAW_CMD_RESTART = 3,
    CLAW_CMD_STATUS = 4,
    CLAW_CMD_LIST = 5,
    CLAW_CMD_ISOLATE = 6,
    CLAW_CMD_RELOAD = 7,
    CLAW_CMD_SHUTDOWN = 8
} ipc_command_t;

/* IPC message types */
typedef enum {
    IPC_REQUEST = 0,
    IPC_RESPONSE = 1,
    IPC_NOTIFICATION = 2
} ipc_message_type_t;

/* Runtime configuration paths.
 * Defaults point to the standard install locations.
 * All paths can be shifted by setting the CLAW_PREFIX environment variable,
 * which is useful for testing against a staged sysroot. */
struct claw_paths {
    const char *config_dir;    /* /etc/claw              */
    const char *state_dir;     /* /var/lib/claw           */
    const char *log_dir;       /* /var/log/claw           */
    const char *run_dir;       /* /run/claw               */
};

const struct claw_paths *claw_get_paths(void);

#endif /* __CLAW_H__ */
