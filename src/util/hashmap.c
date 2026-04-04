#include "hashmap.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define HASHMAP_INITIAL_CAPACITY 64
#define HASHMAP_LOAD_FACTOR 0.75

/* Simple hash function for strings */
static uint32_t hash_string(const char *str) {
    uint32_t hash = 5381;
    int c;

    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;  /* hash * 33 + c */
    }

    return hash;
}

hashmap_t *hashmap_new(void) {
    return hashmap_with_capacity(HASHMAP_INITIAL_CAPACITY);
}

hashmap_t *hashmap_with_capacity(size_t capacity) {
    hashmap_t *m = malloc(sizeof(hashmap_t));
    if (!m) return NULL;

    m->entries = calloc(capacity, sizeof(hashmap_entry_t));
    if (!m->entries) {
        free(m);
        return NULL;
    }

    m->capacity = capacity;
    m->count = 0;
    return m;
}

void hashmap_free(hashmap_t *m) {
    if (!m) return;

    for (size_t i = 0; i < m->capacity; i++) {
        if (m->entries[i].key) {
            free(m->entries[i].key);
            free(m->entries[i].value);
        }
    }

    free(m->entries);
    free(m);
}

void hashmap_free_shell(hashmap_t *m) {
    if (!m) return;

    for (size_t i = 0; i < m->capacity; i++) {
        if (m->entries[i].key) {
            free(m->entries[i].key);
        }
    }

    free(m->entries);
    free(m);
}

static int hashmap_grow(hashmap_t *m) {
    size_t old_capacity = m->capacity;
    hashmap_entry_t *old_entries = m->entries;

    m->capacity *= 2;
    m->entries = calloc(m->capacity, sizeof(hashmap_entry_t));
    if (!m->entries) {
        m->capacity = old_capacity;
        m->entries = old_entries;
        return -1;
    }

    /* Rehash all entries */
    m->count = 0;
    for (size_t i = 0; i < old_capacity; i++) {
        if (old_entries[i].key) {
            hashmap_set(m, old_entries[i].key, old_entries[i].value);
            free(old_entries[i].key);
        }
    }

    free(old_entries);
    return 0;
}

int hashmap_set(hashmap_t *m, const char *key, void *value) {
    if (!m || !key) return -1;

    /* Check if resize needed */
    if (m->count >= (size_t)(m->capacity * HASHMAP_LOAD_FACTOR)) {
        if (hashmap_grow(m) != 0) return -1;
    }

    uint32_t hash = hash_string(key);
    size_t index = hash % m->capacity;

    /* Linear probing for collision resolution */
    for (size_t i = 0; i < m->capacity; i++) {
        size_t pos = (index + i) % m->capacity;

        if (!m->entries[pos].key) {
            /* Empty slot, insert new entry */
            m->entries[pos].key = malloc(strlen(key) + 1);
            if (!m->entries[pos].key) return -1;

            strcpy(m->entries[pos].key, key);
            m->entries[pos].value = value;
            m->count++;
            return 0;
        } else if (strcmp(m->entries[pos].key, key) == 0) {
            /* Key exists, update value */
            m->entries[pos].value = value;
            return 0;
        }
    }

    return -1;  /* Table is full (shouldn't happen) */
}

void *hashmap_get(hashmap_t *m, const char *key) {
    if (!m || !key) return NULL;

    uint32_t hash = hash_string(key);
    size_t index = hash % m->capacity;

    for (size_t i = 0; i < m->capacity; i++) {
        size_t pos = (index + i) % m->capacity;

        if (!m->entries[pos].key) {
            return NULL;  /* Not found */
        }

        if (strcmp(m->entries[pos].key, key) == 0) {
            return m->entries[pos].value;
        }
    }

    return NULL;
}

bool hashmap_has(hashmap_t *m, const char *key) {
    return hashmap_get(m, key) != NULL;
}

void *hashmap_remove(hashmap_t *m, const char *key) {
    if (!m || !key) return NULL;

    uint32_t hash = hash_string(key);
    size_t index = hash % m->capacity;

    /* Find the entry */
    size_t pos = m->capacity;
    for (size_t i = 0; i < m->capacity; i++) {
        size_t p = (index + i) % m->capacity;
        if (!m->entries[p].key) return NULL;  /* Not found */
        if (strcmp(m->entries[p].key, key) == 0) { pos = p; break; }
    }
    if (pos == m->capacity) return NULL;

    void *value = m->entries[pos].value;
    free(m->entries[pos].key);
    m->entries[pos].key   = NULL;
    m->entries[pos].value = NULL;
    m->count--;

    /*
     * Backward-shift deletion (Knuth's Algorithm R):
     * Scan forward from pos+1; whenever an entry's ideal slot is "on the way"
     * to the current hole, move it into the hole.
     * Condition: (hole - ideal + N) % N < (cur - ideal + N) % N
     */
    size_t h = pos;
    size_t N = m->capacity;
    for (size_t i = (h + 1) % N; m->entries[i].key; i = (i + 1) % N) {
        size_t k = hash_string(m->entries[i].key) % N;
        if ((h - k + N) % N < (i - k + N) % N) {
            m->entries[h] = m->entries[i];
            m->entries[i].key   = NULL;
            m->entries[i].value = NULL;
            h = i;
        }
    }

    return value;
}

size_t hashmap_size(hashmap_t *m) {
    return m ? m->count : 0;
}

void hashmap_clear(hashmap_t *m) {
    if (!m) return;

    for (size_t i = 0; i < m->capacity; i++) {
        if (m->entries[i].key) {
            free(m->entries[i].key);
            m->entries[i].key = NULL;
            m->entries[i].value = NULL;
        }
    }

    m->count = 0;
}

char **hashmap_keys(hashmap_t *m, size_t *out_count) {
    if (!m) {
        if (out_count) *out_count = 0;
        return NULL;
    }

    char **keys = malloc(m->count * sizeof(char *));
    if (!keys) return NULL;

    size_t index = 0;
    for (size_t i = 0; i < m->capacity && index < m->count; i++) {
        if (m->entries[i].key) {
            keys[index++] = m->entries[i].key;
        }
    }

    if (out_count) *out_count = index;
    return keys;
}

void **hashmap_values(hashmap_t *m, size_t *out_count) {
    if (!m) {
        if (out_count) *out_count = 0;
        return NULL;
    }

    void **values = malloc(m->count * sizeof(void *));
    if (!values) return NULL;

    size_t index = 0;
    for (size_t i = 0; i < m->capacity && index < m->count; i++) {
        if (m->entries[i].key) {
            values[index++] = m->entries[i].value;
        }
    }

    if (out_count) *out_count = index;
    return values;
}

void hashmap_foreach(hashmap_t *m,
                    void (*callback)(const char *key, void *value, void *ctx),
                    void *ctx) {
    if (!m || !callback) return;

    for (size_t i = 0; i < m->capacity; i++) {
        if (m->entries[i].key) {
            callback(m->entries[i].key, m->entries[i].value, ctx);
        }
    }
}
