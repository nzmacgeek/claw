#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/select.h>

#include "claw.h"
#include "log.h"
#include "mem.h"
#include "config.h"
#include "dag.h"
#include "supervisor.h"
#include "ipc.h"
#include "os.h"
#include "state.h"

/* -----------------------------------------------------------------------
 * Global daemon state
 * --------------------------------------------------------------------- */

static volatile int g_sig_child   = 0;
static volatile int g_sig_term    = 0;
static volatile int g_sig_int     = 0;
static volatile int g_sig_hup     = 0;

static config_t     *g_config     = NULL;
static dag_t        *g_dag        = NULL;
static supervisor_t *g_supervisor = NULL;
static state_db_t   *g_state      = NULL;

#define STATE_DB_FILENAME "state.db"

static const char *g_config_dir  = NULL;   /* Set from claw_get_paths() or -C flag */
static const char *g_socket_path = NULL;
static int           g_ipc_fd     = -1;   /* Listening socket */
static char          g_state_path[512];   /* Built from claw_get_paths()->state_dir */
static char          g_socket_path_buf[512];

typedef struct {
    int   single_user;
    char *target_override;
} boot_options_t;

static int is_truthy(const char *value) {
    return value
        && (strcmp(value, "1") == 0
         || strcmp(value, "true") == 0
         || strcmp(value, "yes") == 0
         || strcmp(value, "on") == 0);
}

static int token_requests_single_user(const char *token) {
    return strcmp(token, "single") == 0
        || strcmp(token, "s") == 0
        || strcmp(token, "S") == 0
        || strcmp(token, "1") == 0
        || strcmp(token, "rescue") == 0
        || strcmp(token, "emergency") == 0;
}

static void load_boot_options(boot_options_t *opts) {
    if (!opts) return;

    memset(opts, 0, sizeof(*opts));

    FILE *f = fopen("/proc/cmdline", "r");
    if (!f) {
        log_debug("boot", "Cannot read /proc/cmdline: %s", strerror(errno));
        return;
    }

    char cmdline[4096];
    if (!fgets(cmdline, sizeof(cmdline), f)) {
        fclose(f);
        return;
    }
    fclose(f);

    char *saveptr = NULL;
    for (char *tok = strtok_r(cmdline, " \t\r\n", &saveptr);
         tok;
         tok = strtok_r(NULL, " \t\r\n", &saveptr)) {
        if (token_requests_single_user(tok)) {
            opts->single_user = 1;
            continue;
        }

        if (strncmp(tok, "claw.single=", 12) == 0) {
            opts->single_user = is_truthy(tok + 12);
            continue;
        }

        if (strncmp(tok, "claw.target=", 12) == 0 && tok[12] != '\0') {
            free(opts->target_override);
            opts->target_override = mem_strdup(tok + 12);
        }
    }
}

static void free_boot_options(boot_options_t *opts) {
    if (!opts) return;
    free(opts->target_override);
    opts->target_override = NULL;
}

static int init_default_socket_path(const struct claw_paths *paths) {
    int n;

    if (!paths || !paths->run_dir)
        return -1;

    n = snprintf(g_socket_path_buf, sizeof(g_socket_path_buf),
                 "%s/claw.sock", paths->run_dir);
    if (n < 0 || (size_t)n >= sizeof(g_socket_path_buf))
        return -1;

    g_socket_path = g_socket_path_buf;
    return 0;
}

static void apply_log_config(void) {
    const char *log_dir = claw_get_paths()->log_dir;
    log_level_t level = LOG_INFO;

    if (g_config) {
        if (g_config->log_dir && *g_config->log_dir)
            log_dir = g_config->log_dir;
        level = g_config->log_level;
    }

    if (log_init(log_dir, level) != 0)
        log_error("init", "Failed to reinitialize logging for %s", log_dir);
}

static int run_single_user_shell(void) {
    extern char **environ;

    const char *shell_path = "/bin/bash";
    char *const bash_argv[] = { "bash", "-l", NULL };
    char *const sh_argv[]   = { "sh", NULL };
    char *const *shell_argv = bash_argv;

    if (access(shell_path, X_OK) != 0) {
        if (access("/bin/sh", X_OK) != 0) {
            log_error("boot", "Single-user shell not available: neither /bin/bash nor /bin/sh exists");
            return -1;
        }
        shell_path = "/bin/sh";
        shell_argv = sh_argv;
        log_warning("boot", "/bin/bash not available; falling back to /bin/sh");
    }

    for (;;) {
        int console_fd = open("/dev/console", O_RDWR);
        if (console_fd < 0)
            console_fd = open("/dev/tty", O_RDWR);
        if (console_fd < 0) {
            log_error("boot", "Cannot open a system console for single-user mode: %s",
                      strerror(errno));
            return -1;
        }

        log_info("boot", "Launching single-user shell: %s", shell_path);

        pid_t pid = fork();
        if (pid < 0) {
            log_error("boot", "fork() failed for single-user shell: %s", strerror(errno));
            close(console_fd);
            return -1;
        }

        if (pid == 0) {
            setsid();
            ioctl(console_fd, TIOCSCTTY, 0);
            dup2(console_fd, STDIN_FILENO);
            dup2(console_fd, STDOUT_FILENO);
            dup2(console_fd, STDERR_FILENO);
            if (console_fd > STDERR_FILENO)
                close(console_fd);
            if (chdir("/") != 0)
                _exit(126);
            execve(shell_path, shell_argv, environ);
            _exit(127);
        }

        close(console_fd);

        for (;;) {
            int status = 0;
            pid_t waited = waitpid(pid, &status, 0);
            if (waited == pid) {
                if (WIFEXITED(status)) {
                    log_warning("boot", "Single-user shell exited with status %d; respawning",
                                WEXITSTATUS(status));
                } else if (WIFSIGNALED(status)) {
                    log_warning("boot", "Single-user shell terminated by signal %d; respawning",
                                WTERMSIG(status));
                } else {
                    log_warning("boot", "Single-user shell exited; respawning");
                }
                break;
            }

            if (waited < 0 && errno == EINTR) {
                if (g_sig_term || g_sig_int) {
                    kill(pid, SIGTERM);
                    waitpid(pid, NULL, 0);
                    return 0;
                }
                continue;
            }

            if (waited < 0) {
                log_error("boot", "waitpid() failed for single-user shell: %s", strerror(errno));
                return -1;
            }
        }
    }
}

/* -----------------------------------------------------------------------
 * Signal handling
 * --------------------------------------------------------------------- */

static void handle_sig(int sig) {
    switch (sig) {
        case SIGCHLD: g_sig_child = 1; break;
        case SIGTERM: g_sig_term  = 1; break;
        case SIGINT:  g_sig_int   = 1; break;
        case SIGHUP:  g_sig_hup   = 1; break;
    }
}

static void setup_signal_handlers(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sig;
    sigemptyset(&sa.sa_mask);
    /* Don't restart syscalls that can be interrrupted by signals
     * so that select() wakes up on signal delivery. */
    sa.sa_flags = 0;

    sigaction(SIGCHLD, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGHUP,  &sa, NULL);
    signal(SIGPIPE, SIG_IGN);
}

/* -----------------------------------------------------------------------
 * Child reaping (PID 1 responsibility)
 * --------------------------------------------------------------------- */

static void reap_children(void) {
    int    wstatus;
    pid_t  pid;

    while ((pid = waitpid(-1, &wstatus, WNOHANG)) > 0) {
        if (g_supervisor)
            supervisor_handle_exit(g_supervisor, pid, wstatus);
    }
}

/* -----------------------------------------------------------------------
 * IPC request handling
 * --------------------------------------------------------------------- */

/* Build a simple JSON response */
static void respond(int client_fd, const char *status, const char *detail) {
    char body[512];
    snprintf(body, sizeof(body),
             "{\"status\":\"%s\",\"detail\":\"%s\"}",
             status, detail ? detail : "");
    ipc_send(client_fd, IPC_RESPONSE, (ipc_command_t)0, body);
}

/* Extract "unit" value from simple JSON {"unit":"<name>"} */
static int extract_unit(const char *json, char *out, size_t out_len) {
    const char *key = "\"unit\":\"";
    const char *p = strstr(json, key);
    if (!p) return -1;
    p += strlen(key);
    const char *end = strchr(p, '"');
    if (!end) return -1;
    size_t len = (size_t)(end - p);
    if (len >= out_len) len = out_len - 1;
    strncpy(out, p, len);
    out[len] = '\0';
    return 0;
}

static void handle_ipc_request(int client_fd, const ipc_msg_t *msg) {
    char unit[128] = {0};
    extract_unit(msg->body, unit, sizeof(unit));

    switch ((ipc_command_t)msg->header.command) {

        case CLAW_CMD_START:
            if (!*unit) { respond(client_fd, "error", "missing unit name"); break; }
            if (supervisor_start(g_supervisor, unit) == 0)
                respond(client_fd, "ok", "started");
            else
                respond(client_fd, "error", "start failed");
            break;

        case CLAW_CMD_STOP:
            if (!*unit) { respond(client_fd, "error", "missing unit name"); break; }
            if (supervisor_stop(g_supervisor, unit) == 0)
                respond(client_fd, "ok", "stopped");
            else
                respond(client_fd, "error", "stop failed");
            break;

        case CLAW_CMD_RESTART:
            if (!*unit) { respond(client_fd, "error", "missing unit name"); break; }
            if (supervisor_restart(g_supervisor, unit) == 0)
                respond(client_fd, "ok", "restarted");
            else
                respond(client_fd, "error", "restart failed");
            break;

        case CLAW_CMD_STATUS: {
            if (!*unit) { respond(client_fd, "error", "missing unit name"); break; }
            service_t *svc = hashmap_get(g_config->services, unit);
            if (svc) {
                char body[256];
                snprintf(body, sizeof(body),
                         "{\"unit\":\"%s\",\"state\":\"%s\",\"pid\":%d}",
                         svc->name,
                         service_state_name(svc->state),
                         svc->main_pid);
                ipc_send(client_fd, IPC_RESPONSE, (ipc_command_t)0, body);
            } else {
                respond(client_fd, "error", "unit not found");
            }
            break;
        }

        case CLAW_CMD_LIST: {
            /* Build JSON array of {name, state} objects */
            char buf[4096] = "{\"units\":[";
            size_t pos = strlen(buf);
            int first  = 1;

            size_t count = 0;
            char **keys  = hashmap_keys(g_config->services, &count);
            for (size_t i = 0; i < count; i++) {
                service_t *svc = hashmap_get(g_config->services, keys[i]);
                if (!svc) continue;
                int n = snprintf(buf + pos, sizeof(buf) - pos - 4,
                                 "%s{\"name\":\"%s\",\"state\":\"%s\"}",
                                 first ? "" : ",",
                                 svc->name,
                                 service_state_name(svc->state));
                if (n > 0) pos += n;
                first = 0;
            }
            free(keys);

            if (pos < sizeof(buf) - 2) {
                buf[pos++] = ']';
                buf[pos++] = '}';
                buf[pos]   = '\0';
            }
            ipc_send(client_fd, IPC_RESPONSE, (ipc_command_t)0, buf);
            break;
        }

        case CLAW_CMD_RELOAD:
            respond(client_fd, "ok", "reload scheduled");
            g_sig_hup = 1;
            break;

        case CLAW_CMD_SHUTDOWN:
            respond(client_fd, "ok", "shutdown initiated");
            g_sig_term = 1;
            break;

        case CLAW_CMD_ISOLATE:
            respond(client_fd, "error", "isolate not yet implemented");
            break;

        default:
            respond(client_fd, "error", "unknown command");
            break;
    }
}

static void poll_ipc(void) {
    if (g_ipc_fd < 0) return;

    /* Non-blocking accept loop */
    for (;;) {
        int client = ipc_server_accept(g_ipc_fd);
        if (client < 0) break;  /* EAGAIN / no pending connections */

        ipc_msg_t msg;
        if (ipc_recv(client, &msg) == 0)
            handle_ipc_request(client, &msg);

        close(client);
    }
}

/* -----------------------------------------------------------------------
 * State sync — copy live supervisor data into state DB
 * --------------------------------------------------------------------- */

static void sync_state(void) {
    if (!g_state || !g_config) return;

    size_t count = 0;
    char **keys = hashmap_keys(g_config->services, &count);
    for (size_t i = 0; i < count; i++) {
        service_t *svc = hashmap_get(g_config->services, keys[i]);
        if (!svc) continue;
        int fail_count = 0;
        state_entry_t *prev = state_db_get_service(g_state, svc->name);
        if (prev) fail_count = prev->fail_count
            + (svc->state == SERVICE_FAILED ? 1 : 0);
        state_db_set_service(g_state, svc->name, svc->state,
                             svc->restart_count, fail_count,
                             svc->started_at, svc->stopped_at);
    }
    free(keys);
    state_db_set_system(g_state, SYSTEM_RUNTIME);
    state_db_save(g_state, g_state_path);
}

/* -----------------------------------------------------------------------
 * Shutdown
 * --------------------------------------------------------------------- */

static void do_shutdown(const char *reason) {
    log_shutdown(reason);
    fprintf(stdout, "\n[claw] The Magic Claw has spoken. Everyone must go now.\n\n");

    if (g_config && g_supervisor) {
        /* Stop all active services in reverse topo order */
        size_t count = 0;
        char **keys  = hashmap_keys(g_config->services, &count);
        for (size_t i = 0; i < count; i++) {
            service_t *svc = hashmap_get(g_config->services, keys[i]);
            if (svc && (svc->state == SERVICE_ACTIVE || svc->state == SERVICE_ACTIVATING))
                supervisor_stop(g_supervisor, svc->name);
        }
        free(keys);
    }

    ipc_server_destroy(g_ipc_fd, g_socket_path);

    /* Save state before freeing structures */
    if (g_state) {
        sync_state();
        state_db_mark_shutdown(g_state);
        state_db_set_system(g_state, SYSTEM_SHUTDOWN);
        state_db_save(g_state, g_state_path);
        state_db_free(g_state);
        g_state = NULL;
    }

    supervisor_free(g_supervisor);
    dag_free(g_dag);
    config_free(g_config);
    log_cleanup();
}

/* -----------------------------------------------------------------------
 * Boot
 * --------------------------------------------------------------------- */

static int do_boot(void) {
    boot_options_t boot_opts;

    /* ---- Early OS setup (PID 1 only) ---- */
    if (getpid() == 1) {
        log_boot_stage("os", "Mounting early filesystems");
        if (os_mount_early() != 0)
            log_warning("boot", "Some early mounts failed — continuing anyway");

        log_boot_stage("os", "Setting hostname");
        os_set_hostname();

        log_boot_stage("os", "Seeding kernel PRNG");
        os_seed_random();
    }

    /* Always create runtime dirs (they can't hurt in test env) */
    os_create_runtime_dirs();

    load_boot_options(&boot_opts);

    if (boot_opts.single_user) {
        if (boot_opts.target_override) {
            log_info("boot", "Ignoring claw.target=%s because single-user mode was requested",
                     boot_opts.target_override);
        }

        if (getpid() == 1) {
            log_boot_stage("os", "Mounting /etc/fstab entries");
            os_mount_fstab();
        }

        log_warning("boot", "Kernel cmdline requested single-user mode; skipping normal service startup");
        log_system_state(SYSTEM_BASIC);

        int shell_rc = run_single_user_shell();
        free_boot_options(&boot_opts);
        return shell_rc == 0 ? 1 : -1;
    }

    /* Load persistent state (detect unclean previous shutdown) */
    g_state = state_db_new();
    if (state_db_load(g_state, g_state_path) == 0) {
        state_entry_t *prev = NULL;
        if (!g_state->clean_shutdown && g_state->boot_time > 0) {
            log_warning("boot", "Previous shutdown was NOT clean — checking for failed services");
        }
        (void)prev;
    }
    state_db_mark_boot(g_state);
    state_db_set_system(g_state, SYSTEM_INIT);

    char cfg_msg[256];
    snprintf(cfg_msg, sizeof(cfg_msg), "Loading unit files from %s", g_config_dir);
    log_boot_stage("config", cfg_msg);

    g_config = config_new();
    if (config_load(g_config, g_config_dir) != 0) {
        log_error("boot", "Failed to load configuration");
        free_boot_options(&boot_opts);
        return -1;
    }
    apply_log_config();

    log_boot_stage("dag", "Building dependency graph");
    g_dag = dag_new();
    if (dag_build_from_config(g_dag, g_config) != 0) {
        log_error("boot", "Dependency graph has cycles — cannot boot");
        free_boot_options(&boot_opts);
        return -1;
    }

    g_supervisor = supervisor_new(g_config->services);

    log_boot_stage("ipc", "Starting IPC socket");
    g_ipc_fd = ipc_server_create(g_socket_path);
    if (g_ipc_fd < 0)
        log_warning("boot", "IPC socket unavailable — clawctl will not work");

    log_system_state(SYSTEM_EARLY_BOOT);

    /* Mount remaining filesystems from fstab (PID 1 only) */
    if (getpid() == 1) {
        log_boot_stage("os", "Mounting /etc/fstab entries");
        os_mount_fstab();
    }

    /* Activate the default target */
    const char *default_target = boot_opts.target_override
        ? boot_opts.target_override
        : g_config->default_target;
    if (boot_opts.target_override) {
        log_info("boot", "Kernel cmdline overrides default target: %s", default_target);
    }

    if (default_target && *default_target) {
        log_info("boot", "[claw] The Magic Claw chooses: %s", default_target);

        vector_t *order = dag_activation_order(g_dag, default_target);
        if (!order) {
            log_error("boot", "Cannot resolve activation order for %s", default_target);
        } else {
            size_t started = 0;
            for (size_t i = 0; i < vector_length(order); i++) {
                dag_node_t *node = vector_get(order, i);

                /* Only fork services — targets are just synchronization points */
                if (node->unit_type != UNIT_SERVICE) continue;

                log_info("boot", "Activating service: %s", node->name);
                if (supervisor_start(g_supervisor, node->name) == 0)
                    started++;
                else
                    log_warning("boot", "Failed to start %s", node->name);
            }
            log_info("boot", "Boot activation complete: %zu service(s) started", started);
            vector_free_shell(order);
        }
    } else {
        log_warning("boot", "No default_target set — nothing will be started");
    }

    free_boot_options(&boot_opts);

    log_system_state(SYSTEM_RUNTIME);
    fprintf(stdout, "\n[claw] The Magic Claw rests... but watches everything.\n\n");

    return 0;
}

/* -----------------------------------------------------------------------
 * Main
 * --------------------------------------------------------------------- */

int main(int argc, char *argv[]) {
    /* PID 1 failures are otherwise hard to diagnose on early bring-up.
     * Keep stdout/stderr unbuffered so every message reaches the console/log. */
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    /* Initialise runtime paths.  CLAW_PREFIX can shift all paths for testing
     * against a staged sysroot.  The -C flag below overrides config_dir. */
    const struct claw_paths *paths = claw_get_paths();
    g_config_dir = paths->config_dir;
    if (init_default_socket_path(paths) != 0) {
        fprintf(stderr,
                "[claw] Failed to initialise socket path from %s\n",
                paths->run_dir ? paths->run_dir : "<null>");
        return 1;
    }
    int state_path_len = snprintf(g_state_path, sizeof(g_state_path),
                                  "%s/" STATE_DB_FILENAME, paths->state_dir);
    if (state_path_len < 0 || (size_t)state_path_len >= sizeof(g_state_path)) {
        fprintf(stderr,
                "[claw] Failed to initialise state DB path: '%s/%s' does not fit in buffer\n",
                paths->state_dir, STATE_DB_FILENAME);
        return 1;
    }

    /* Parse arguments: [-C <config_dir>] [-S <socket_path>] */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-C") == 0 && i + 1 < argc) {
            g_config_dir = argv[++i];
        } else if (strcmp(argv[i], "-S") == 0 && i + 1 < argc) {
            g_socket_path = argv[++i];
        } else {
            fprintf(stderr, "Usage: %s [-C <config_dir>] [-S <socket_path>]\n", argv[0]);
            return 1;
        }
    }

    log_init(paths->log_dir, LOG_INFO);

    fprintf(stdout, "\n");
    fprintf(stdout, "[claw] The Magic Claw awakens...\n");
    fprintf(stdout, "[claw] Choosing who will go first...\n");
    fprintf(stdout, "[claw] config=%s socket=%s pid=%d\n", g_config_dir, g_socket_path, getpid());
    fprintf(stdout, "\n");

    log_info("init", "Claw " CLAW_VERSION " (build " CLAW_BUILD_ID ") starting (PID %d)", getpid());
    log_system_state(SYSTEM_INIT);

    setup_signal_handlers();

    int boot_rc = do_boot();
    if (boot_rc < 0) {
        log_critical("init", "Boot failed");
        log_cleanup();
        return 1;
    }

    if (boot_rc > 0) {
        do_shutdown(g_sig_term ? "SIGTERM" : g_sig_int ? "SIGINT" : "single-user mode exit");
        return 0;
    }

    /* ---- Main event loop ---- */
    log_info("init", "Entering main event loop");

    int loop_tick = 0;
    while (!g_sig_term && !g_sig_int) {
        if (g_sig_child) {
            g_sig_child = 0;
            reap_children();
        }
        if (g_sig_hup) {
            g_sig_hup = 0;
            log_info("init", "SIGHUP — reloading configuration");
            config_free(g_config);
            dag_free(g_dag);
            g_config = config_new();
            config_load(g_config, g_config_dir);
            apply_log_config();
            g_dag = dag_new();
            dag_build_from_config(g_dag, g_config);
            g_supervisor->services = g_config->services;
        }

        supervisor_check_timeouts(g_supervisor);
        poll_ipc();

        /* Persist state every 30 seconds */
        if (++loop_tick >= 30) {
            loop_tick = 0;
            sync_state();
        }

        /* Sleep up to 1 second or until signal */
        struct timeval tv = {1, 0};
        int     maxfd = g_ipc_fd + 1;
        fd_set  rfds;
        FD_ZERO(&rfds);
        if (g_ipc_fd >= 0) FD_SET(g_ipc_fd, &rfds);
        select(maxfd, &rfds, NULL, NULL, &tv);
    }

    do_shutdown(g_sig_term ? "SIGTERM" : "SIGINT");
    return 0;
}
