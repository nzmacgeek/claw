#include "supervisor.h"
#include "mem.h"
#include "log.h"
#include "claw-time.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <pwd.h>
#include <grp.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/time.h>

#define LOG_DIR "/var/log/claw"

/* -----------------------------------------------------------------------
 * Helpers
 * --------------------------------------------------------------------- */

/* Open a per-service log file for capturing stdout/stderr.
 * Returns fd or -1 on error. */
static int open_service_log(const char *service_name, const char *suffix) {
    char path[256];
    snprintf(path, sizeof(path), "%s/%s.%s.log", LOG_DIR, service_name, suffix);
    return open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
}

/* Drop privileges to the given user/group.
 * Called in the child process before exec. */
static int drop_privileges(const char *user, const char *group) {
    /* Set group first (need root to change group) */
    if (group && *group) {
        struct group *gr = getgrnam(group);
        if (!gr) {
            /* Silently continue — group may not exist in embedded env */
        } else if (setgid(gr->gr_gid) != 0) {
            return -1;
        }
    }

    if (user && *user && strcmp(user, "root") != 0) {
        struct passwd *pw = getpwnam(user);
        if (!pw) {
            return -1;
        }
        if (setuid(pw->pw_uid) != 0) {
            return -1;
        }
    }

    return 0;
}

/* Build a NULL-terminated environ array merging parent env with
 * service overrides.  Returned array must be freed by caller. */
static char **build_env(char **service_env) {
    /* Count parent environment */
    extern char **environ;
    int parent_count = 0;
    if (environ) {
        while (environ[parent_count]) parent_count++;
    }

    int extra_count = 0;
    if (service_env) {
        while (service_env[extra_count]) extra_count++;
    }

    /* Combined env: parent + service overrides */
    char **env = mem_calloc(parent_count + extra_count + 1, sizeof(char *));

    int idx = 0;
    if (environ) {
        for (int i = 0; i < parent_count; i++) {
            env[idx++] = mem_strdup(environ[i]);
        }
    }
    if (service_env) {
        for (int i = 0; i < extra_count; i++) {
            env[idx++] = mem_strdup(service_env[i]);
        }
    }
    env[idx] = NULL;
    return env;
}

static void free_env(char **env) {
    if (!env) return;
    for (int i = 0; env[i]; i++) free(env[i]);
    free(env);
}

/* Build a copy of argv with $MAINPID substituted for the given pid.
 * Returns a newly allocated NULL-terminated array; caller must free_env() it. */
static char **subst_args(char **args, pid_t pid) {
    int count = 0;
    while (args[count]) count++;

    char **out = mem_calloc(count + 1, sizeof(char *));
    char pid_str[32];
    snprintf(pid_str, sizeof(pid_str), "%d", (int)pid);

    for (int i = 0; i < count; i++) {
        /* Simple token-level substitution of $MAINPID */
        if (strcmp(args[i], "$MAINPID") == 0) {
            out[i] = mem_strdup(pid_str);
        } else {
            out[i] = mem_strdup(args[i]);
        }
    }
    out[count] = NULL;
    return out;
}

/* -----------------------------------------------------------------------
 * supervisor_t lifecycle
 * --------------------------------------------------------------------- */

supervisor_t *supervisor_new(hashmap_t *services) {
    supervisor_t *sv = mem_calloc(1, sizeof(supervisor_t));
    sv->services  = services;
    sv->epoll_fd  = -1;
    return sv;
}

void supervisor_free(supervisor_t *sv) {
    if (!sv) return;
    /* services is owned by config, not us */
    free(sv);
}

/* -----------------------------------------------------------------------
 * Start a service
 * --------------------------------------------------------------------- */

int supervisor_start(supervisor_t *sv, const char *name) {
    if (!sv || !name) return -1;

    service_t *svc = hashmap_get(sv->services, name);
    if (!svc) {
        log_error("supervisor", "Unknown service: %s", name);
        return -1;
    }

    /* Already running? */
    if (svc->state == SERVICE_ACTIVE || svc->state == SERVICE_ACTIVATING) {
        log_warning("supervisor", "Service already active: %s", name);
        return 0;
    }

    if (!svc->start_cmd || !svc->start_args) {
        log_error("supervisor", "Service has no start_cmd: %s", name);
        svc->state = SERVICE_FAILED;
        return -1;
    }

    log_service_start(name);
    svc->state = SERVICE_ACTIVATING;

    /* oneshot services don't get supervised after exit */
    int is_oneshot = (svc->type == SERVICE_ONESHOT);

    /* Open log files before forking */
    int stdout_fd = open_service_log(name, "stdout");
    int stderr_fd = open_service_log(name, "stderr");

    /* Build environment for the child */
    char **env = build_env(svc->env);

    pid_t pid = fork();

    if (pid < 0) {
        log_error("supervisor", "fork() failed for %s: %s", name, strerror(errno));
        svc->state = SERVICE_FAILED;
        if (stdout_fd >= 0) close(stdout_fd);
        if (stderr_fd >= 0) close(stderr_fd);
        free_env(env);
        return -1;
    }

    if (pid == 0) {
        /* ---- Child process ---- */

        /* Redirect stdout/stderr to log files */
        if (stdout_fd >= 0) {
            dup2(stdout_fd, STDOUT_FILENO);
            close(stdout_fd);
        }
        if (stderr_fd >= 0) {
            dup2(stderr_fd, STDERR_FILENO);
            close(stderr_fd);
        }

        /* Close stdin */
        int null_fd = open("/dev/null", O_RDONLY);
        if (null_fd >= 0) {
            dup2(null_fd, STDIN_FILENO);
            close(null_fd);
        }

        /* Change working directory */
        if (svc->working_dir && *svc->working_dir) {
            if (chdir(svc->working_dir) != 0) {
                const char *msg = "chdir failed\n";
                ssize_t n = write(STDERR_FILENO, msg, 13); (void)n;
            }
        }

        /* Drop privileges */
        if (drop_privileges(svc->user, svc->group) != 0) {
            const char *msg = "failed to drop privileges\n";
            ssize_t n = write(STDERR_FILENO, msg, 26); (void)n;
            _exit(1);
        }

        /* For forking daemons create a new session */
        if (svc->type == SERVICE_FORKING) {
            setsid();
        }

        /* Replace process image */
        execve(svc->start_cmd, svc->start_args, env);

        /* execve only returns on error */
        const char *msg = "execve failed\n";
        ssize_t n = write(STDERR_FILENO, msg, 14); (void)n;
        _exit(127);
    }

    /* ---- Parent process ---- */
    free_env(env);
    if (stdout_fd >= 0) close(stdout_fd);
    if (stderr_fd >= 0) close(stderr_fd);

    svc->main_pid     = pid;
    svc->started_at   = time(NULL);
    svc->log_stdout_fd = -1;
    svc->log_stderr_fd = -1;

    /*
     * For simple services: we consider them ACTIVE immediately and
     * rely on SIGCHLD to detect failure.
     * For oneshot: stay ACTIVATING until the process exits.
     * For forking: stay ACTIVATING until we find the PID file.
     */
    if (!is_oneshot && svc->type != SERVICE_FORKING) {
        svc->state = SERVICE_ACTIVE;
    }

    log_service_started(name, pid);
    return 0;
}

/* -----------------------------------------------------------------------
 * Stop a service
 * --------------------------------------------------------------------- */

int supervisor_stop(supervisor_t *sv, const char *name) {
    if (!sv || !name) return -1;

    service_t *svc = hashmap_get(sv->services, name);
    if (!svc) {
        log_error("supervisor", "Unknown service: %s", name);
        return -1;
    }

    if (svc->state == SERVICE_INACTIVE || svc->state == SERVICE_DEAD) {
        log_debug("supervisor", "Service already stopped: %s", name);
        return 0;
    }

    log_service_stop(name);
    svc->state = SERVICE_DEACTIVATING;

    /* Use custom stop command if provided */
    if (svc->stop_cmd && svc->stop_args) {
        char **env  = build_env(svc->env);
        char **args = subst_args(svc->stop_args, svc->main_pid);
        pid_t pid = fork();
        if (pid == 0) {
            execve(svc->stop_cmd, args, env);
            _exit(127);
        }
        free_env(args);
        free_env(env);
        if (pid > 0) {
            /* Wait for stop command to complete */
            int status;
            waitpid(pid, &status, 0);
        }
    } else if (svc->main_pid > 0) {
        /* Default: SIGTERM + timeout + SIGKILL */
        kill(svc->main_pid, SIGTERM);

        unsigned int timeout = svc->timeout_stop > 0 ? svc->timeout_stop : 10;
        uint64_t deadline = time_deadline_in_ms(timeout * 1000);

        while (time_until_ms(deadline) > 0) {
            int status;
            pid_t result = waitpid(svc->main_pid, &status, WNOHANG);
            if (result == svc->main_pid) {
                /* Process exited */
                svc->exit_code = WIFEXITED(status) ? WEXITSTATUS(status)
                               : 128 + WTERMSIG(status);
                svc->main_pid = -1;
                svc->state    = SERVICE_INACTIVE;
                svc->stopped_at = time(NULL);
                log_service_stopped(name, svc->exit_code);
                return 0;
            }
            time_sleep_ms(50);
        }

        /* Timeout — force kill */
        log_warning("supervisor", "Service %s did not stop in time, sending SIGKILL", name);
        kill(svc->main_pid, SIGKILL);
        waitpid(svc->main_pid, NULL, 0);
    }

    svc->main_pid   = -1;
    svc->state      = SERVICE_INACTIVE;
    svc->stopped_at = time(NULL);
    log_service_stopped(name, 0);
    return 0;
}

/* -----------------------------------------------------------------------
 * Restart / Reload
 * --------------------------------------------------------------------- */

int supervisor_restart(supervisor_t *sv, const char *name) {
    supervisor_stop(sv, name);
    return supervisor_start(sv, name);
}

int supervisor_reload(supervisor_t *sv, const char *name) {
    if (!sv || !name) return -1;

    service_t *svc = hashmap_get(sv->services, name);
    if (!svc || svc->state != SERVICE_ACTIVE) {
        log_warning("supervisor", "Cannot reload inactive service: %s", name);
        return -1;
    }

    if (svc->reload_cmd && svc->reload_args) {
        /* Run explicit reload command */
        char **env = build_env(svc->env);
        pid_t pid = fork();
        if (pid == 0) {
            execve(svc->reload_cmd, svc->reload_args, env);
            _exit(127);
        }
        free_env(env);
        if (pid > 0) waitpid(pid, NULL, 0);
    } else if (svc->main_pid > 0) {
        /* Default: SIGHUP */
        kill(svc->main_pid, SIGHUP);
    }

    log_info("supervisor", "Reloaded service: %s", name);
    return 0;
}

/* -----------------------------------------------------------------------
 * Handle child process exit (called from SIGCHLD handler path)
 * --------------------------------------------------------------------- */

void supervisor_handle_exit(supervisor_t *sv, pid_t pid, int wstatus) {
    if (!sv || pid <= 0) return;

    /* Find the service owning this PID */
    service_t *svc = NULL;
    size_t count = 0;
    char **keys = hashmap_keys(sv->services, &count);

    for (size_t i = 0; i < count; i++) {
        service_t *s = hashmap_get(sv->services, keys[i]);
        if (s && s->main_pid == pid) {
            svc = s;
            break;
        }
    }
    free(keys);

    if (!svc) {
        /* Orphaned child — PID 1 still needs to reap it */
        log_debug("supervisor", "Reaped untracked child PID %d", pid);
        return;
    }

    int exit_code = WIFEXITED(wstatus)   ? WEXITSTATUS(wstatus)
                  : WIFSIGNALED(wstatus) ? 128 + WTERMSIG(wstatus)
                  : -1;

    svc->exit_code  = exit_code;
    svc->main_pid   = -1;
    svc->stopped_at = time(NULL);

    log_service_stopped(svc->name, exit_code);

    /* State machine transition */
    if (svc->state == SERVICE_DEACTIVATING) {
        svc->state = SERVICE_INACTIVE;
        return;
    }

    /* oneshot: success = active/dead, failure = failed */
    if (svc->type == SERVICE_ONESHOT) {
        svc->state = (exit_code == 0) ? SERVICE_DEAD : SERVICE_FAILED;
        return;
    }

    /* Unexpected exit — apply restart policy */
    int should_restart = 0;

    switch (svc->restart_policy) {
        case RESTART_ALWAYS:
            should_restart = 1;
            break;
        case RESTART_ON_FAILURE:
            should_restart = (exit_code != 0);
            break;
        case RESTART_ON_ABNORMAL:
            should_restart = WIFSIGNALED(wstatus);
            break;
        case RESTART_NO:
        default:
            should_restart = 0;
            break;
    }

    /* Honour restart_max limit */
    if (should_restart && svc->restart_max > 0 &&
        svc->restart_count >= (int)svc->restart_max) {
        log_warning("supervisor", "Service %s hit restart_max (%u), not restarting",
                    svc->name, svc->restart_max);
        should_restart = 0;
    }

    if (should_restart) {
        svc->restart_count++;
        svc->state = SERVICE_RESTARTING;
        log_service_restart(svc->name, "unexpected exit");

        if (svc->restart_delay > 0) {
            /* Schedule restart non-blocking — picked up by check_timeouts() */
            svc->restart_at = time(NULL) + (time_t)svc->restart_delay;
        } else {
            svc->restart_at = 0;
            if (supervisor_start(sv, svc->name) != 0) {
                svc->state = SERVICE_FAILED;
                log_service_failed(svc->name, "restart failed");
            }
        }
    } else {
        svc->state = (exit_code == 0) ? SERVICE_INACTIVE : SERVICE_FAILED;

        if (exit_code != 0) {
            log_service_failed(svc->name, "non-zero exit code");
        }
    }
}

/* -----------------------------------------------------------------------
 * Timeout check — called periodically from main loop
 * --------------------------------------------------------------------- */

void supervisor_check_timeouts(supervisor_t *sv) {
    if (!sv) return;

    time_t now = time(NULL);
    size_t count = 0;
    char **keys  = hashmap_keys(sv->services, &count);

    for (size_t i = 0; i < count; i++) {
        service_t *svc = hashmap_get(sv->services, keys[i]);
        if (!svc) continue;

        if (svc->state == SERVICE_ACTIVATING && svc->started_at > 0) {
            time_t elapsed = now - svc->started_at;
            if ((unsigned int)elapsed > svc->timeout_start) {
                log_service_failed(svc->name, "start timeout exceeded");
                if (svc->main_pid > 0) {
                    kill(svc->main_pid, SIGKILL);
                    svc->main_pid = -1;
                }
                svc->state = SERVICE_FAILED;
            }
        }

        if (svc->state == SERVICE_RESTARTING && svc->restart_at > 0 && now >= svc->restart_at) {
            svc->restart_at = 0;
            if (supervisor_start(sv, svc->name) != 0) {
                svc->state = SERVICE_FAILED;
                log_service_failed(svc->name, "restart failed");
            }
        }
    }

    free(keys);
}
