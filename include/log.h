#ifndef __LOG_H__
#define __LOG_H__

#include "claw.h"
#include <stdio.h>
#include <time.h>

/* Initialize logging system */
int log_init(const char *log_dir, log_level_t level);

/* Set log level */
void log_set_level(log_level_t level);

/* Get current log level */
log_level_t log_get_level(void);

/* Get the currently configured log directory. */
const char *log_get_dir(void);

/* Core logging functions */
void log_debug(const char *module, const char *fmt, ...);
void log_info(const char *module, const char *fmt, ...);
void log_warning(const char *module, const char *fmt, ...);
void log_error(const char *module, const char *fmt, ...);
void log_critical(const char *module, const char *fmt, ...);

/* Service lifecycle logging */
void log_service_start(const char *service);
void log_service_started(const char *service, pid_t pid);
void log_service_stop(const char *service);
void log_service_stopped(const char *service, int exit_code);
void log_service_failed(const char *service, const char *reason);
void log_service_restart(const char *service, const char *reason);

/* Target logging */
void log_target_activate(const char *target);
void log_target_activated(const char *target);
void log_target_deactivate(const char *target);

/* Boot sequence */
void log_boot_stage(const char *stage, const char *description);
void log_system_state(system_state_t state);

/* Shutdown */
void log_shutdown(const char *reason);

/* Cleanup */
void log_cleanup(void);

#endif /* __LOG_H__ */
