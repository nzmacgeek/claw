#include "target.h"
#include "mem.h"
#include <stdlib.h>

target_t *target_new(void) {
    target_t *tgt = mem_calloc(1, sizeof(target_t));
    tgt->requires  = vector_new();
    tgt->wants     = vector_new();
    tgt->after     = vector_new();
    tgt->before    = vector_new();
    tgt->conflicts = vector_new();
    tgt->state     = TARGET_INACTIVE;
    return tgt;
}

static void free_str_vector(vector_t *v) {
    if (!v) return;
    for (size_t i = 0; i < vector_length(v); i++) {
        free(vector_get(v, i));
    }
    vector_free_shell(v);
}

void target_free(target_t *tgt) {
    if (!tgt) return;
    free(tgt->name);
    free(tgt->description);
    free_str_vector(tgt->requires);
    free_str_vector(tgt->wants);
    free_str_vector(tgt->after);
    free_str_vector(tgt->before);
    free_str_vector(tgt->conflicts);
    free(tgt);
}

const char *target_state_name(target_state_t s) {
    switch (s) {
        case TARGET_INACTIVE:     return "inactive";
        case TARGET_ACTIVATING:   return "activating";
        case TARGET_ACTIVE:       return "active";
        case TARGET_DEACTIVATING: return "deactivating";
        case TARGET_FAILED:       return "failed";
        default:                  return "unknown";
    }
}
