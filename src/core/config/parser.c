#include "config.h"
#include "mem.h"
#include "log.h"
#include "claw-string.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>

/* -----------------------------------------------------------------------
 * Internal helpers
 * --------------------------------------------------------------------- */

/* Strip inline comment (#) from value */
static void strip_comment(char *s) {
    /* Don't strip # inside quotes — for simplicity we only look for bare # */
    char *p = s;
    while (*p) {
        if (*p == '#') { *p = '\0'; break; }
        p++;
    }
}

/* Trim in-place */
static char *trim_inplace(char *s) {
    /* Leading */
    while (*s == ' ' || *s == '\t') s++;
    /* Trailing */
    char *end = s + strlen(s) - 1;
    while (end > s && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r'))
        *end-- = '\0';
    return s;
}

/* Strip surrounding quotes from a value (single or double) */
static char *strip_quotes(char *s) {
    size_t len = strlen(s);
    if (len >= 2 &&
        ((s[0] == '"' && s[len-1] == '"') ||
         (s[0] == '\'' && s[len-1] == '\''))) {
        s[len-1] = '\0';
        return s + 1;
    }
    return s;
}

/*
 * Append whitespace-separated tokens from a string into a vector.
 * Handles both "a b c" and "a, b, c" separators.
 */
static void tokenize_into_vector(const char *str, vector_t *v) {
    if (!str || !v) return;

    char *copy = mem_strdup(str);
    char *tok  = strtok(copy, " \t,");
    while (tok) {
        tok = trim_inplace(tok);
        if (*tok) vector_append(v, mem_strdup(tok));
        tok = strtok(NULL, " \t,");
    }
    free(copy);
}

/* Parse "key: value" line into key/value pair.
 * Returns 1 on success, 0 if line is not a k:v pair. */
static int parse_kv_line(char *line, char **out_key, char **out_val) {
    char *colon = strchr(line, ':');
    if (!colon) return 0;

    *colon = '\0';
    *out_key = trim_inplace(line);
    *out_val = trim_inplace(colon + 1);
    strip_comment(*out_val);
    *out_val = trim_inplace(*out_val);
    *out_val = strip_quotes(*out_val);
    return 1;
}

/* Parse restart policy string */
static restart_policy_t parse_restart_policy(const char *s) {
    if (!s)                         return RESTART_ON_FAILURE;
    if (strcmp(s, "always") == 0)   return RESTART_ALWAYS;
    if (strcmp(s, "on-failure") == 0) return RESTART_ON_FAILURE;
    if (strcmp(s, "on-abnormal") == 0) return RESTART_ON_ABNORMAL;
    if (strcmp(s, "no") == 0)       return RESTART_NO;
    return RESTART_ON_FAILURE;
}

/* Parse service type string */
static service_type_t parse_service_type(const char *s) {
    if (!s)                           return SERVICE_SIMPLE;
    if (strcmp(s, "simple") == 0)     return SERVICE_SIMPLE;
    if (strcmp(s, "forking") == 0)    return SERVICE_FORKING;
    if (strcmp(s, "oneshot") == 0)    return SERVICE_ONESHOT;
    if (strcmp(s, "notify") == 0)     return SERVICE_NOTIFY;
    if (strcmp(s, "timer") == 0)      return SERVICE_TIMER;
    return SERVICE_SIMPLE;
}

/* Parse log level string */
static log_level_t parse_log_level(const char *s) {
    if (!s)                            return LOG_INFO;
    if (strcmp(s, "debug") == 0)       return LOG_DEBUG;
    if (strcmp(s, "info") == 0)        return LOG_INFO;
    if (strcmp(s, "warning") == 0)     return LOG_WARNING;
    if (strcmp(s, "error") == 0)       return LOG_ERROR;
    if (strcmp(s, "critical") == 0)    return LOG_CRITICAL;
    return LOG_INFO;
}

/* -----------------------------------------------------------------------
 * Service YAML parser
 * --------------------------------------------------------------------- */

int config_parse_service(const char *path, service_t *svc) {
    FILE *f = fopen(path, "r");
    if (!f) {
        log_error("config", "Cannot open service file: %s", path);
        return -1;
    }

    char line[1024];
    int  ok = 1;

    while (fgets(line, sizeof(line), f)) {
        /* Skip blank lines and comments */
        char *trimmed = trim_inplace(line);
        if (!*trimmed || *trimmed == '#') continue;

        char *key, *val;
        if (!parse_kv_line(trimmed, &key, &val)) continue;

        if      (strcmp(key, "name") == 0)
            svc->name = mem_strdup(val);
        else if (strcmp(key, "description") == 0)
            svc->description = mem_strdup(val);
        else if (strcmp(key, "type") == 0)
            svc->type = parse_service_type(val);
        else if (strcmp(key, "start_cmd") == 0 || strcmp(key, "exec_start") == 0) {
            /* Split command into argv */
            char **args = service_parse_args(val);
            if (args) {
                free(svc->start_cmd);
                svc->start_cmd  = mem_strdup(args[0]);
                service_free_args(svc->start_args);
                svc->start_args = args;
            }
        }
        else if (strcmp(key, "stop_cmd") == 0 || strcmp(key, "exec_stop") == 0) {
            char **args = service_parse_args(val);
            if (args) {
                free(svc->stop_cmd);
                svc->stop_cmd  = mem_strdup(args[0]);
                service_free_args(svc->stop_args);
                svc->stop_args = args;
            }
        }
        else if (strcmp(key, "reload_cmd") == 0 || strcmp(key, "exec_reload") == 0) {
            char **args = service_parse_args(val);
            if (args) {
                free(svc->reload_cmd);
                svc->reload_cmd  = mem_strdup(args[0]);
                service_free_args(svc->reload_args);
                svc->reload_args = args;
            }
        }
        else if (strcmp(key, "user") == 0) {
            free(svc->user); svc->user = mem_strdup(val);
        }
        else if (strcmp(key, "group") == 0) {
            free(svc->group); svc->group = mem_strdup(val);
        }
        else if (strcmp(key, "working_dir") == 0 || strcmp(key, "working_directory") == 0) {
            free(svc->working_dir); svc->working_dir = mem_strdup(val);
        }
        else if (strcmp(key, "requires") == 0)
            tokenize_into_vector(val, svc->requires);
        else if (strcmp(key, "wants") == 0)
            tokenize_into_vector(val, svc->wants);
        else if (strcmp(key, "after") == 0)
            tokenize_into_vector(val, svc->after);
        else if (strcmp(key, "before") == 0)
            tokenize_into_vector(val, svc->before);
        else if (strcmp(key, "restart") == 0 || strcmp(key, "restart_policy") == 0)
            svc->restart_policy = parse_restart_policy(val);
        else if (strcmp(key, "restart_delay") == 0)
            svc->restart_delay = (unsigned int)atoi(val);
        else if (strcmp(key, "restart_max") == 0)
            svc->restart_max = (unsigned int)atoi(val);
        else if (strcmp(key, "timeout_start") == 0)
            svc->timeout_start = (unsigned int)atoi(val);
        else if (strcmp(key, "timeout_stop") == 0)
            svc->timeout_stop = (unsigned int)atoi(val);
        else if (strcmp(key, "pid_file") == 0) {
            free(svc->pid_file); svc->pid_file = mem_strdup(val);
        }
        else if (strcmp(key, "notify_socket") == 0) {
            free(svc->notify_socket); svc->notify_socket = mem_strdup(val);
        }
        else if (strcmp(key, "environment") == 0 || strcmp(key, "env") == 0) {
            /* env: KEY=VALUE KEY2=VALUE2 */
            int count = 0;
            char **pairs = string_parse_kv_pairs(val, &count);
            if (pairs && count > 0) {
                /* Merge with existing env */
                char **new_env = mem_calloc(count + 1, sizeof(char *));
                for (int i = 0; i < count; i++)
                    new_env[i] = pairs[i];
                new_env[count] = NULL;
                service_free_args(svc->env);
                svc->env = new_env;
                free(pairs); /* Free array, strings now owned by new_env */
            }
        }
        /* Unrecognised keys are silently ignored */
    }

    fclose(f);

    if (!svc->name) {
        log_error("config", "Service file has no 'name' field: %s", path);
        ok = 0;
    }
    if (!svc->start_cmd) {
        log_error("config", "Service '%s' has no 'start_cmd': %s",
                  svc->name ? svc->name : "(unknown)", path);
        ok = 0;
    }

    return ok ? 0 : -1;
}

/* -----------------------------------------------------------------------
 * Target YAML parser
 * --------------------------------------------------------------------- */

int config_parse_target(const char *path, target_t *tgt) {
    FILE *f = fopen(path, "r");
    if (!f) {
        log_error("config", "Cannot open target file: %s", path);
        return -1;
    }

    char line[1024];
    int  ok = 1;

    while (fgets(line, sizeof(line), f)) {
        char *trimmed = trim_inplace(line);
        if (!*trimmed || *trimmed == '#') continue;

        char *key, *val;
        if (!parse_kv_line(trimmed, &key, &val)) continue;

        if      (strcmp(key, "name") == 0)
            tgt->name = mem_strdup(val);
        else if (strcmp(key, "description") == 0)
            tgt->description = mem_strdup(val);
        else if (strcmp(key, "requires") == 0)
            tokenize_into_vector(val, tgt->requires);
        else if (strcmp(key, "wants") == 0)
            tokenize_into_vector(val, tgt->wants);
        else if (strcmp(key, "after") == 0)
            tokenize_into_vector(val, tgt->after);
        else if (strcmp(key, "before") == 0)
            tokenize_into_vector(val, tgt->before);
        else if (strcmp(key, "conflicts") == 0)
            tokenize_into_vector(val, tgt->conflicts);
        else if (strcmp(key, "default_target") == 0 || strcmp(key, "default") == 0)
            tgt->is_default = (strcmp(val, "true") == 0 || strcmp(val, "yes") == 0);
        else if (strcmp(key, "isolate") == 0)
            tgt->isolate = (strcmp(val, "true") == 0 || strcmp(val, "yes") == 0);
    }

    fclose(f);

    if (!tgt->name) {
        log_error("config", "Target file has no 'name' field: %s", path);
        ok = 0;
    }

    return ok ? 0 : -1;
}

/* -----------------------------------------------------------------------
 * Main config loader
 * --------------------------------------------------------------------- */

config_t *config_new(void) {
    config_t *cfg = mem_calloc(1, sizeof(config_t));
    cfg->services      = hashmap_new();
    cfg->targets       = hashmap_new();
    cfg->log_level     = LOG_INFO;
    cfg->log_dir       = mem_strdup("/var/log/claw");
    cfg->default_target = mem_strdup("claw-multiuser.target");
    return cfg;
}

void config_free(config_t *cfg) {
    if (!cfg) return;

    /* Free all services */
    size_t count = 0;
    char **keys = hashmap_keys(cfg->services, &count);
    for (size_t i = 0; i < count; i++) {
        service_t *svc = hashmap_get(cfg->services, keys[i]);
        service_free(svc);
    }
    free(keys);
    hashmap_free_shell(cfg->services);

    /* Free all targets */
    keys = hashmap_keys(cfg->targets, &count);
    for (size_t i = 0; i < count; i++) {
        target_t *tgt = hashmap_get(cfg->targets, keys[i]);
        target_free(tgt);
    }
    free(keys);
    hashmap_free_shell(cfg->targets);

    free(cfg->default_target);
    free(cfg->log_dir);
    free(cfg);
}

/* Load main claw.conf */
static void load_main_conf(config_t *cfg, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return;  /* Optional */

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char *trimmed = trim_inplace(line);
        if (!*trimmed || *trimmed == '#') continue;

        char *key, *val;
        if (!parse_kv_line(trimmed, &key, &val)) continue;

        if (strcmp(key, "default_target") == 0) {
            free(cfg->default_target);
            cfg->default_target = mem_strdup(val);
        } else if (strcmp(key, "log_level") == 0) {
            cfg->log_level = parse_log_level(val);
        } else if (strcmp(key, "log_dir") == 0) {
            free(cfg->log_dir);
            cfg->log_dir = mem_strdup(val);
        }
    }

    fclose(f);
}

/* Load all *.yml / *.yaml files from a directory with a callback */
static void load_yml_dir(const char *dir,
                         void (*cb)(const char *path, config_t *cfg),
                         config_t *cfg) {
    DIR *d = opendir(dir);
    if (!d) return;

    struct dirent *entry;
    while ((entry = readdir(d))) {
        const char *name = entry->d_name;
        size_t len = strlen(name);
        int is_yml = (len > 4 && strcmp(name + len - 4, ".yml")  == 0) ||
                     (len > 5 && strcmp(name + len - 5, ".yaml") == 0);
        if (!is_yml) continue;

        /* Build full path */
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", dir, name);

        /* Only regular files */
        struct stat st;
        if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
            cb(path, cfg);
        }
    }

    closedir(d);
}

static void load_service_file(const char *path, config_t *cfg) {
    service_t *svc = service_new();
    if (config_parse_service(path, svc) == 0) {
        hashmap_set(cfg->services, svc->name, svc);
        log_debug("config", "Loaded service: %s", svc->name);
    } else {
        service_free(svc);
    }
}

static void load_target_file(const char *path, config_t *cfg) {
    target_t *tgt = target_new();
    if (config_parse_target(path, tgt) == 0) {
        hashmap_set(cfg->targets, tgt->name, tgt);
        log_debug("config", "Loaded target: %s", tgt->name);
    } else {
        target_free(tgt);
    }
}

int config_load(config_t *cfg, const char *dir) {
    if (!cfg || !dir) return -1;

    char path[512];

    /* Main config */
    snprintf(path, sizeof(path), "%s/claw.conf", dir);
    load_main_conf(cfg, path);

    /* Service files */
    snprintf(path, sizeof(path), "%s/services.d", dir);
    load_yml_dir(path, load_service_file, cfg);

    /* Target files */
    snprintf(path, sizeof(path), "%s/targets.d", dir);
    load_yml_dir(path, load_target_file, cfg);

    log_info("config", "Loaded %zu service(s), %zu target(s) from %s",
             hashmap_size(cfg->services),
             hashmap_size(cfg->targets),
             dir);

    return 0;
}
