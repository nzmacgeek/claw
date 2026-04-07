#include "service.h"
#include "mem.h"
#include <stdlib.h>
#include <string.h>

service_t *service_new(void) {
    service_t *svc = mem_calloc(1, sizeof(service_t));
    svc->aliases         = vector_new();
    svc->requires        = vector_new();
    svc->wants           = vector_new();
    svc->after           = vector_new();
    svc->before          = vector_new();
    svc->state           = SERVICE_INACTIVE;
    svc->main_pid        = -1;
    svc->exit_code       = -1;
    svc->timeout_start   = 30;
    svc->timeout_stop    = 10;
    svc->restart_policy  = RESTART_ON_FAILURE;
    svc->restart_delay   = 5;
    svc->restart_max     = 0;   /* unlimited */
    svc->log_stdout_fd   = -1;
    svc->log_stderr_fd   = -1;
    return svc;
}

static void free_argv(char **args) {
    if (!args) return;
    for (int i = 0; args[i]; i++) free(args[i]);
    free(args);
}

static void free_str_vector(vector_t *v) {
    if (!v) return;
    for (size_t i = 0; i < vector_length(v); i++) {
        free(vector_get(v, i));
    }
    vector_free_shell(v);
}

void service_free(service_t *svc) {
    if (!svc) return;
    free(svc->name);
    free(svc->description);
    free(svc->start_cmd);
    free(svc->stop_cmd);
    free(svc->reload_cmd);
    free(svc->working_dir);
    free(svc->user);
    free(svc->group);
    free(svc->pid_file);
    free(svc->notify_socket);
    free_argv(svc->start_args);
    free_argv(svc->stop_args);
    free_argv(svc->reload_args);
    free_argv(svc->env);
    free_str_vector(svc->aliases);
    free_str_vector(svc->requires);
    free_str_vector(svc->wants);
    free_str_vector(svc->after);
    free_str_vector(svc->before);
    free(svc);
}

char **service_parse_args(const char *exec) {
    if (!exec) return NULL;

    /* Count tokens */
    int count = 0;
    const char *p = exec;
    int in_token = 0;
    while (*p) {
        if (*p == ' ' || *p == '\t') {
            in_token = 0;
        } else {
            if (!in_token) { count++; in_token = 1; }
        }
        p++;
    }

    if (count == 0) return NULL;

    char **args = mem_calloc(count + 1, sizeof(char *));

    /* Duplicate a modifiable copy for strtok */
    char *copy = mem_strdup(exec);
    int   idx = 0;
    char *tok = strtok(copy, " \t");
    while (tok && idx < count) {
        args[idx++] = mem_strdup(tok);
        tok = strtok(NULL, " \t");
    }
    args[idx] = NULL;
    free(copy);
    return args;
}

void service_free_args(char **args) {
    free_argv(args);
}

const char *service_state_name(service_state_t s) {
    switch (s) {
        case SERVICE_INACTIVE:     return "inactive";
        case SERVICE_ACTIVATING:   return "activating";
        case SERVICE_ACTIVE:       return "active";
        case SERVICE_DEACTIVATING: return "deactivating";
        case SERVICE_RESTARTING:   return "restarting";
        case SERVICE_FAILED:       return "failed";
        case SERVICE_DEAD:         return "dead";
        default:                   return "unknown";
    }
}

const char *service_type_name(service_type_t t) {
    switch (t) {
        case SERVICE_SIMPLE:   return "simple";
        case SERVICE_FORKING:  return "forking";
        case SERVICE_ONESHOT:  return "oneshot";
        case SERVICE_NOTIFY:   return "notify";
        case SERVICE_TIMER:    return "timer";
        default:               return "unknown";
    }
}
