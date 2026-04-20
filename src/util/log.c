#include "log.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

static log_level_t current_level = LOG_INFO;
static char *log_dir = NULL;
static int log_fd = -1;
static int error_fd = -1;
static const char *level_names[] = {
    "DEBUG",
    "INFO",
    "WARNING",
    "ERROR",
    "CRITICAL"
};

static const char *level_colors[] = {
    "\033[36m",  /* Cyan for DEBUG */
    "\033[32m",  /* Green for INFO */
    "\033[33m",  /* Yellow for WARNING */
    "\033[31m",  /* Red for ERROR */
    "\033[1;31m" /* Bold red for CRITICAL */
};

static const char *color_reset = "\033[0m";
static const char *syslog_socket_path = "/run/log/yap.inbox";
static const char *syslog_log_path = "/var/log/system.log";

static int map_syslog_priority(log_level_t level) {
    int severity = 6; /* LOG_INFO */

    switch (level) {
        case LOG_DEBUG:
            severity = 7;
            break;
        case LOG_INFO:
            severity = 6;
            break;
        case LOG_WARNING:
            severity = 4;
            break;
        case LOG_ERROR:
            severity = 3;
            break;
        case LOG_CRITICAL:
            severity = 2;
            break;
    }

    return (3 << 3) | severity; /* LOG_DAEMON */
}

static int connect_syslog_socket(void) {
    if (access(syslog_socket_path, F_OK) != 0) {
        return -1;
    }

    int fd = open(syslog_socket_path, O_WRONLY | O_APPEND);
    if (fd < 0) {
        return -1;
    }

    return fd;
}

static int write_syslog_payload(int fd, const char *payload, size_t len) {
    size_t offset = 0;

    while (offset < len) {
        ssize_t written = write(fd, payload + offset, len - offset);
        if (written > 0) {
            offset += (size_t)written;
            continue;
        }
        if (written < 0 && errno == EINTR) {
            continue;
        }
        return -1;
    }

    return 0;
}

static void mirror_to_syslog_file(const char *payload, size_t len) {
    int fd;
    struct stat st;

    if (stat(syslog_log_path, &st) != 0 || st.st_size == 0) {
        return;
    }

    fd = open(syslog_log_path, O_WRONLY | O_APPEND);
    if (fd < 0) {
        return;
    }

    (void)write_syslog_payload(fd, payload, len);
    close(fd);
}

static void mirror_to_syslog(log_level_t level, const char *module, const char *message) {
    char payload[1024];
    int fd;
    int len;

    len = snprintf(
        payload,
        sizeof(payload),
        "<%d>claw/%s[%d]: %s\n",
        map_syslog_priority(level),
        module ? module : "core",
        getpid(),
        message
    );
    if (len <= 0) {
        return;
    }
    if ((size_t)len >= sizeof(payload)) {
        len = (int)sizeof(payload) - 1;
    }

    fd = connect_syslog_socket();
    if (fd >= 0) {
        (void)write_syslog_payload(fd, payload, (size_t)len);
        close(fd);
    }

    mirror_to_syslog_file(payload, (size_t)len);
}

static int ensure_dir_recursive(const char *path, mode_t mode) {
    char buf[512];
    size_t len;

    if (!path || !*path) return -1;

    len = strlen(path);
    if (len >= sizeof(buf)) return -1;

    strcpy(buf, path);
    for (char *p = buf + 1; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        if (mkdir(buf, 0755) != 0 && errno != EEXIST)
            return -1;
        *p = '/';
    }

    if (mkdir(buf, mode) != 0 && errno != EEXIST)
        return -1;

    return 0;
}

int log_init(const char *dir, log_level_t level) {
    if (!dir) return -1;

    log_cleanup();

    log_dir = malloc(strlen(dir) + 1);
    if (!log_dir) return -1;

    strcpy(log_dir, dir);
    current_level = level;

    /* Create log directory if needed */
    ensure_dir_recursive(log_dir, 0755);

    /* Open main log file */
    char log_path[256];
    snprintf(log_path, sizeof(log_path), "%s/claw.log", log_dir);

    log_fd = open(log_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (log_fd < 0) {
        log_fd = open("/dev/null", O_WRONLY);
    }

    /* Open error log file */
    char error_path[256];
    snprintf(error_path, sizeof(error_path), "%s/claw-error.log", log_dir);

    error_fd = open(error_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (error_fd < 0) {
        error_fd = open("/dev/null", O_WRONLY);
    }

    return 0;
}

void log_set_level(log_level_t level) {
    current_level = level;
}

log_level_t log_get_level(void) {
    return current_level;
}

const char *log_get_dir(void) {
    return log_dir;
}

static void log_vprintf(log_level_t level, const char *module, const char *fmt, va_list ap) {
    char message[768];

    if (level < current_level) {
        return;
    }

    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);

    /* Format message */
    char buffer[1024];
    vsnprintf(message, sizeof(message), fmt, ap);
    int len = snprintf(buffer, sizeof(buffer), "[%s] [%-8s] [%-15s] ",
                       timestamp, level_names[level], module ? module : "core");

    snprintf(buffer + len, sizeof(buffer) - (size_t)len, "%s", message);

    /* Log to console with colors */
    if (level >= LOG_WARNING) {
        /* Errors go to stderr */
        fprintf(stderr, "%s%s%s\n", level_colors[level], buffer, color_reset);
        fflush(stderr);
    } else {
        /* Info/debug to stdout */
        fprintf(stdout, "%s%s%s\n", level_colors[level], buffer, color_reset);
        fflush(stdout);
    }

    /* Log to file */
    if (log_fd >= 0) {
        dprintf(log_fd, "%s\n", buffer);
    }

    /* Log errors to error file */
    if ((level == LOG_ERROR || level == LOG_CRITICAL) && error_fd >= 0) {
        dprintf(error_fd, "%s\n", buffer);
    }

    mirror_to_syslog(level, module, message);
}

void log_debug(const char *module, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    log_vprintf(LOG_DEBUG, module, fmt, ap);
    va_end(ap);
}

void log_info(const char *module, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    log_vprintf(LOG_INFO, module, fmt, ap);
    va_end(ap);
}

void log_warning(const char *module, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    log_vprintf(LOG_WARNING, module, fmt, ap);
    va_end(ap);
}

void log_error(const char *module, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    log_vprintf(LOG_ERROR, module, fmt, ap);
    va_end(ap);
}

void log_critical(const char *module, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    log_vprintf(LOG_CRITICAL, module, fmt, ap);
    va_end(ap);
}

void log_service_start(const char *service) {
    log_info("service-lifecycle", "Starting service: %s", service);
}

void log_service_started(const char *service, pid_t pid) {
    log_info("service-lifecycle", "Started service: %s (PID: %d)", service, pid);
}

void log_service_stop(const char *service) {
    log_info("service-lifecycle", "Stopping service: %s", service);
}

void log_service_stopped(const char *service, int exit_code) {
    log_info("service-lifecycle", "Stopped service: %s (exit code: %d)", service, exit_code);
}

void log_service_failed(const char *service, const char *reason) {
    log_error("service-error", "Service failed (%s): %s", service, reason);
}

void log_service_restart(const char *service, const char *reason) {
    log_info("service-lifecycle", "Restarting service %s (%s)", service, reason);
}

void log_target_activate(const char *target) {
    log_info("target-lifecycle", "Activating target: %s", target);
}

void log_target_activated(const char *target) {
    log_info("target-lifecycle", "Target activated: %s", target);
}

void log_target_deactivate(const char *target) {
    log_info("target-lifecycle", "Deactivating target: %s", target);
}

void log_boot_stage(const char *stage, const char *description) {
    log_info("boot", "Boot stage: %s - %s", stage, description);
}

void log_system_state(system_state_t state) {
    const char *state_names[] = {
        "init", "early-boot", "devices", "rootfs", "basic", "network",
        "multiuser", "graphical", "runtime", "shutdown", "off"
    };

    if (state < 11) {
        log_info("system-state", "System state: %s", state_names[state]);
    }
}

void log_shutdown(const char *reason) {
    log_info("shutdown", "System shutdown: %s", reason);
}

void log_cleanup(void) {
    if (log_fd >= 0) {
        close(log_fd);
        log_fd = -1;
    }

    if (error_fd >= 0) {
        close(error_fd);
        error_fd = -1;
    }

    if (log_dir) {
        free(log_dir);
        log_dir = NULL;
    }
}
