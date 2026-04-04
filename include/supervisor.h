#ifndef __SUPERVISOR_H__
#define __SUPERVISOR_H__

#include "service.h"
#include "hashmap.h"
#include <sys/types.h>

/*
 * supervisor_t holds the runtime supervision state for all managed services.
 * One instance lives for the lifetime of the claw daemon.
 */
typedef struct {
    hashmap_t *services;    /* name → service_t* (shared with config) */
    int        epoll_fd;    /* For integrating timeouts with event loop */
} supervisor_t;

/* Create / destroy the supervisor */
supervisor_t *supervisor_new(hashmap_t *services);
void          supervisor_free(supervisor_t *sv);

/*
 * Start a service:
 *   - forks + execs
 *   - sets up stdout/stderr log capture
 *   - transitions state to ACTIVATING → ACTIVE (simple) or records pid (forking)
 * Returns 0 on success, -1 on error.
 */
int supervisor_start(supervisor_t *sv, const char *name);

/*
 * Stop a service:
 *   - sends SIGTERM; waits up to timeout_stop seconds
 *   - sends SIGKILL if still running
 * Returns 0 on success, -1 on error.
 */
int supervisor_stop(supervisor_t *sv, const char *name);

/* Restart = stop then start */
int supervisor_restart(supervisor_t *sv, const char *name);

/* Send SIGHUP to reload */
int supervisor_reload(supervisor_t *sv, const char *name);

/*
 * Called from PID 1's SIGCHLD handler after waitpid().
 * Updates service state and applies restart policy.
 */
void supervisor_handle_exit(supervisor_t *sv, pid_t pid, int wstatus);

/* Scan all ACTIVATING services; mark FAILED if start timeout exceeded */
void supervisor_check_timeouts(supervisor_t *sv);

#endif /* __SUPERVISOR_H__ */
