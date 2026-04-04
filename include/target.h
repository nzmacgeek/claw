#ifndef __TARGET_H__
#define __TARGET_H__

#include "claw.h"
#include "vector.h"

typedef struct target {
    /* Identity */
    char *name;
    char *description;

    /* Dependencies (string names) */
    vector_t *requires;   /* Units that must be active */
    vector_t *wants;      /* Optional units */
    vector_t *after;      /* Ordering: activate after these */
    vector_t *before;     /* Ordering: activate before these */
    vector_t *conflicts;  /* Cannot be active simultaneously */

    /* Flags */
    int is_default;       /* Boot activates this target */
    int isolate;          /* clawctl isolate: stop units not in this target */

    /* Runtime */
    target_state_t state;
} target_t;

/* Allocate and zero a new target */
target_t *target_new(void);

/* Deep-free a target */
void target_free(target_t *tgt);

/* State name for logging */
const char *target_state_name(target_state_t s);

#endif /* __TARGET_H__ */
