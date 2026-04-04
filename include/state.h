#ifndef __STATE_H__
#define __STATE_H__

/*
 * state.h — System and service state persistence.
 *
 * Tracks the last-known state of all units and persists it to disk so
 * claw can detect abrupt shutdowns, unclean service states, and past
 * failure counts across reboots.
 *
 * File: /var/lib/claw/state.db  (simple key=value, one per line)
 * Format:
 *   system.state=RUNTIME
 *   boot.timestamp=1740000000
 *   service.<name>.state=ACTIVE
 *   service.<name>.restart_count=3
 */

#include "claw.h"
#include "hashmap.h"
#include <time.h>

/* Per-unit persistent state record */
typedef struct {
    char            *name;
    service_state_t  state;
    int              restart_count;
    int              fail_count;
    time_t           last_start;
    time_t           last_stop;
} state_entry_t;

/* In-memory state database */
typedef struct {
    hashmap_t     *entries;      /* name → state_entry_t* */
    system_state_t system_state;
    time_t         boot_time;     /* Unix timestamp of last clean boot */
    int            clean_shutdown; /* 1 if last shutdown was graceful */
} state_db_t;

/* Allocate a new empty state DB */
state_db_t *state_db_new(void);

/* Free state DB (frees all entries) */
void state_db_free(state_db_t *db);

/*
 * Load state from path (silently succeeds if file doesn't exist).
 * Returns 0 on success, -1 on parse error.
 */
int state_db_load(state_db_t *db, const char *path);

/*
 * Persist state to path (atomic: writes to path.tmp then renames).
 * Returns 0 on success, -1 on failure.
 */
int state_db_save(const state_db_t *db, const char *path);

/* Update a service's state in memory */
void state_db_set_service(state_db_t *db, const char *name,
                           service_state_t st,
                           int restart_count, int fail_count,
                           time_t last_start, time_t last_stop);

/* Look up a service entry (NULL if not recorded) */
state_entry_t *state_db_get_service(const state_db_t *db, const char *name);

/* Update the system-level state */
void state_db_set_system(state_db_t *db, system_state_t st);

/* Mark boot timestamp to now */
void state_db_mark_boot(state_db_t *db);

/* Mark clean shutdown */
void state_db_mark_shutdown(state_db_t *db);

#endif /* __STATE_H__ */
