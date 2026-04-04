#ifndef __SERVICE_H__
#define __SERVICE_H__

#include "claw.h"
#include "vector.h"
#include <sys/types.h>
#include <time.h>

/* Restart policies */
typedef enum {
    RESTART_NO,           /* Never restart */
    RESTART_ON_FAILURE,   /* Restart if exit code != 0 or killed by signal */
    RESTART_ON_ABNORMAL,  /* Restart only if killed by signal */
    RESTART_ALWAYS        /* Always restart */
} restart_policy_t;

/* Service definition (parsed from YAML, persisted in memory) */
typedef struct service {
    /* Identity */
    char *name;
    char *description;

    /* Type */
    service_type_t type;

    /* Execution */
    char *start_cmd;        /* Path to executable */
    char **start_args;      /* argv array (NULL-terminated), includes start_cmd[0] */
    char *stop_cmd;
    char **stop_args;
    char *reload_cmd;
    char **reload_args;
    char *working_dir;

    /* Credentials */
    char *user;
    char *group;

    /* Environment (array of "KEY=VALUE" strings, NULL-terminated) */
    char **env;

    /* Dependencies (string names resolved against the unit hashmap) */
    vector_t *requires;   /* Hard requires — if they fail, this fails */
    vector_t *wants;      /* Soft wants — failures are tolerated */
    vector_t *after;      /* Start after these units are active */
    vector_t *before;     /* Start before these units are active */

    /* Runtime state */
    service_state_t state;
    pid_t main_pid;
    int   exit_code;
    int   restart_count;

    /* Timestamps (seconds since epoch) */
    time_t started_at;
    time_t stopped_at;

    /* Timeouts (seconds) */
    unsigned int timeout_start;
    unsigned int timeout_stop;

    /* Restart */
    restart_policy_t restart_policy;
    unsigned int     restart_delay;   /* Seconds between restarts */
    unsigned int     restart_max;     /* 0 = unlimited */
    time_t           restart_at;      /* Monotonic deadline for next restart (0 = immediate) */

    /* Forking / notify */
    char *pid_file;         /* Used by SERVICE_FORKING */
    char *notify_socket;    /* Used by SERVICE_NOTIFY */

    /* Log capture */
    int  log_stdout_fd;
    int  log_stderr_fd;
} service_t;

/* Allocate and zero a new service */
service_t *service_new(void);

/* Deep-free a service */
void service_free(service_t *svc);

/* Parse a NULL-terminated command string into argv */
char **service_parse_args(const char *exec);

/* Free a NULL-terminated argv */
void service_free_args(char **args);

/* State name for logging */
const char *service_state_name(service_state_t s);

/* Type name for logging */
const char *service_type_name(service_type_t t);

#endif /* __SERVICE_H__ */
