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

int log_init(const char *dir, log_level_t level) {
    if (!dir) return -1;

    log_dir = malloc(strlen(dir) + 1);
    if (!log_dir) return -1;

    strcpy(log_dir, dir);
    current_level = level;

    /* Create log directory if needed */
    mkdir(log_dir, 0755);

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

static void log_vprintf(log_level_t level, const char *module, const char *fmt, va_list ap) {
    if (level < current_level) {
        return;
    }

    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);

    /* Format message */
    char buffer[1024];
    int len = snprintf(buffer, sizeof(buffer), "[%s] [%-8s] [%-15s] ",
                       timestamp, level_names[level], module ? module : "core");

    vsnprintf(buffer + len, sizeof(buffer) - len, fmt, ap);

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
