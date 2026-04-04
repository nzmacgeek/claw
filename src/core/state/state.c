#include "state.h"
#include "mem.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#define STATE_PATH_DEFAULT "/var/lib/claw/state.db"

/* -----------------------------------------------------------------------
 * String conversion helpers
 * --------------------------------------------------------------------- */

static const char *service_state_str(service_state_t s) {
    switch (s) {
        case SERVICE_INACTIVE:     return "INACTIVE";
        case SERVICE_ACTIVATING:   return "ACTIVATING";
        case SERVICE_ACTIVE:       return "ACTIVE";
        case SERVICE_DEACTIVATING: return "DEACTIVATING";
        case SERVICE_RESTARTING:   return "RESTARTING";
        case SERVICE_FAILED:       return "FAILED";
        case SERVICE_DEAD:         return "DEAD";
        default:                   return "UNKNOWN";
    }
}

static service_state_t service_state_from_str(const char *s) {
    if (!s)                          return SERVICE_INACTIVE;
    if (strcmp(s, "INACTIVE") == 0)  return SERVICE_INACTIVE;
    if (strcmp(s, "ACTIVATING") == 0)return SERVICE_ACTIVATING;
    if (strcmp(s, "ACTIVE") == 0)    return SERVICE_ACTIVE;
    if (strcmp(s, "DEACTIVATING") == 0)return SERVICE_DEACTIVATING;
    if (strcmp(s, "RESTARTING") == 0)return SERVICE_RESTARTING;
    if (strcmp(s, "FAILED") == 0)    return SERVICE_FAILED;
    if (strcmp(s, "DEAD") == 0)      return SERVICE_DEAD;
    return SERVICE_INACTIVE;
}

static const char *system_state_str(system_state_t s) {
    switch (s) {
        case SYSTEM_INIT:       return "INIT";
        case SYSTEM_EARLY_BOOT: return "EARLY_BOOT";
        case SYSTEM_DEVICES:    return "DEVICES";
        case SYSTEM_ROOTFS:     return "ROOTFS";
        case SYSTEM_BASIC:      return "BASIC";
        case SYSTEM_NETWORK:    return "NETWORK";
        case SYSTEM_MULTIUSER:  return "MULTIUSER";
        case SYSTEM_GRAPHICAL:  return "GRAPHICAL";
        case SYSTEM_RUNTIME:    return "RUNTIME";
        case SYSTEM_SHUTDOWN:   return "SHUTDOWN";
        case SYSTEM_OFF:        return "OFF";
        default:                return "INIT";
    }
}

static system_state_t system_state_from_str(const char *s) {
    if (!s)                             return SYSTEM_INIT;
    if (strcmp(s, "INIT") == 0)         return SYSTEM_INIT;
    if (strcmp(s, "EARLY_BOOT") == 0)   return SYSTEM_EARLY_BOOT;
    if (strcmp(s, "DEVICES") == 0)      return SYSTEM_DEVICES;
    if (strcmp(s, "ROOTFS") == 0)       return SYSTEM_ROOTFS;
    if (strcmp(s, "BASIC") == 0)        return SYSTEM_BASIC;
    if (strcmp(s, "NETWORK") == 0)      return SYSTEM_NETWORK;
    if (strcmp(s, "MULTIUSER") == 0)    return SYSTEM_MULTIUSER;
    if (strcmp(s, "GRAPHICAL") == 0)    return SYSTEM_GRAPHICAL;
    if (strcmp(s, "RUNTIME") == 0)      return SYSTEM_RUNTIME;
    if (strcmp(s, "SHUTDOWN") == 0)     return SYSTEM_SHUTDOWN;
    if (strcmp(s, "OFF") == 0)          return SYSTEM_OFF;
    return SYSTEM_INIT;
}

/* -----------------------------------------------------------------------
 * Allocation
 * --------------------------------------------------------------------- */

state_db_t *state_db_new(void) {
    state_db_t *db = mem_calloc(1, sizeof(state_db_t));
    db->entries       = hashmap_new();
    db->system_state  = SYSTEM_INIT;
    db->boot_time     = 0;
    db->clean_shutdown = 0;
    return db;
}

static void free_entry(state_entry_t *e) {
    if (!e) return;
    free(e->name);
    free(e);
}

void state_db_free(state_db_t *db) {
    if (!db) return;

    size_t count = 0;
    char **keys = hashmap_keys(db->entries, &count);
    for (size_t i = 0; i < count; i++) {
        state_entry_t *e = hashmap_get(db->entries, keys[i]);
        free_entry(e);
    }
    free(keys);

    hashmap_free_shell(db->entries);
    free(db);
}

/* -----------------------------------------------------------------------
 * Load
 * --------------------------------------------------------------------- */

int state_db_load(state_db_t *db, const char *path) {
    if (!db || !path) return -1;

    FILE *f = fopen(path, "r");
    if (!f) {
        if (errno != ENOENT) {
            log_warning("state", "Cannot open state file %s: %s",
                       path, strerror(errno));
        }
        return 0;  /* Missing = fresh start, not an error */
    }

    char line[512];
    while (fgets(line, (int)sizeof(line), f)) {
        /* Trim trailing newline */
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';

        /* Skip blank lines and comments */
        if (!len || line[0] == '#') continue;

        /* Split at '=' */
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char *key = line;
        const char *val = eq + 1;

        /* Parse top-level keys */
        if (strcmp(key, "system.state") == 0) {
            db->system_state = system_state_from_str(val);
            continue;
        }
        if (strcmp(key, "boot.timestamp") == 0) {
            db->boot_time = (time_t)atol(val);
            continue;
        }
        if (strcmp(key, "clean.shutdown") == 0) {
            db->clean_shutdown = atoi(val);
            continue;
        }

        /* Parse service.* keys: service.<name>.<field> */
        if (strncmp(key, "service.", 8) != 0) continue;
        const char *rest = key + 8;  /* "claw-syslog.service.state" */

        /* Find the last '.' to separate service name from field */
        const char *last_dot = strrchr(rest, '.');
        if (!last_dot) continue;

        /* Service name is everything before the last '.' */
        size_t name_len = (size_t)(last_dot - rest);
        char svc_name[256];
        if (name_len >= sizeof(svc_name)) continue;
        memcpy(svc_name, rest, name_len);
        svc_name[name_len] = '\0';

        const char *field = last_dot + 1;

        /* Find or create entry */
        state_entry_t *e = hashmap_get(db->entries, svc_name);
        if (!e) {
            e = mem_calloc(1, sizeof(state_entry_t));
            e->name = mem_strdup(svc_name);
            e->state = SERVICE_INACTIVE;
            hashmap_set(db->entries, svc_name, e);
        }

        if (strcmp(field, "state") == 0) {
            e->state = service_state_from_str(val);
        } else if (strcmp(field, "restart_count") == 0) {
            e->restart_count = atoi(val);
        } else if (strcmp(field, "fail_count") == 0) {
            e->fail_count = atoi(val);
        } else if (strcmp(field, "last_start") == 0) {
            e->last_start = (time_t)atol(val);
        } else if (strcmp(field, "last_stop") == 0) {
            e->last_stop = (time_t)atol(val);
        }
    }

    fclose(f);
    log_debug("state", "Loaded state from %s", path);
    return 0;
}

/* -----------------------------------------------------------------------
 * Save (atomic write)
 * --------------------------------------------------------------------- */

int state_db_save(const state_db_t *db, const char *path) {
    if (!db || !path) return -1;

    /* Write to a temp file, then rename for atomicity */
    char tmp_path[512];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);

    FILE *f = fopen(tmp_path, "w");
    if (!f) {
        log_warning("state", "Cannot write state to %s: %s",
                   tmp_path, strerror(errno));
        return -1;
    }

    /* Header */
    fprintf(f, "# Claw state database — do not edit manually\n");
    fprintf(f, "system.state=%s\n", system_state_str(db->system_state));
    fprintf(f, "boot.timestamp=%ld\n", (long)db->boot_time);
    fprintf(f, "clean.shutdown=%d\n", db->clean_shutdown);
    fprintf(f, "\n");

    /* Service entries */
    size_t count = 0;
    char **keys = hashmap_keys(db->entries, &count);
    for (size_t i = 0; i < count; i++) {
        const state_entry_t *e = hashmap_get(db->entries, keys[i]);
        if (!e) continue;

        fprintf(f, "service.%s.state=%s\n",
                e->name, service_state_str(e->state));
        fprintf(f, "service.%s.restart_count=%d\n",
                e->name, e->restart_count);
        fprintf(f, "service.%s.fail_count=%d\n",
                e->name, e->fail_count);
        fprintf(f, "service.%s.last_start=%ld\n",
                e->name, (long)e->last_start);
        fprintf(f, "service.%s.last_stop=%ld\n",
                e->name, (long)e->last_stop);
        fprintf(f, "\n");
    }
    free(keys);

    fclose(f);

    /* Atomic rename */
    if (rename(tmp_path, path) != 0) {
        log_warning("state", "rename(%s, %s) failed: %s",
                   tmp_path, path, strerror(errno));
        unlink(tmp_path);
        return -1;
    }

    log_debug("state", "State saved to %s (%zu entries)", path, count);
    return 0;
}

/* -----------------------------------------------------------------------
 * Mutation helpers
 * --------------------------------------------------------------------- */

void state_db_set_service(state_db_t *db, const char *name,
                           service_state_t st,
                           int restart_count, int fail_count,
                           time_t last_start, time_t last_stop) {
    if (!db || !name) return;

    state_entry_t *e = hashmap_get(db->entries, name);
    if (!e) {
        e = mem_calloc(1, sizeof(state_entry_t));
        e->name = mem_strdup(name);
        hashmap_set(db->entries, name, e);
    }

    e->state         = st;
    e->restart_count = restart_count;
    e->fail_count    = fail_count;
    e->last_start    = last_start;
    e->last_stop     = last_stop;
}

state_entry_t *state_db_get_service(const state_db_t *db, const char *name) {
    if (!db || !name) return NULL;
    return hashmap_get(db->entries, name);
}

void state_db_set_system(state_db_t *db, system_state_t st) {
    if (!db) return;
    db->system_state = st;
}

void state_db_mark_boot(state_db_t *db) {
    if (!db) return;
    db->boot_time     = time(NULL);
    db->clean_shutdown = 0;  /* Reset — will be set to 1 on clean shutdown */
}

void state_db_mark_shutdown(state_db_t *db) {
    if (!db) return;
    db->clean_shutdown = 1;
}
