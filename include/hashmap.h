#ifndef __HASHMAP_H__
#define __HASHMAP_H__

#include <stddef.h>
#include <stdbool.h>

typedef struct {
    char *key;
    void *value;
} hashmap_entry_t;

typedef struct {
    hashmap_entry_t *entries;
    size_t capacity;
    size_t count;
} hashmap_t;

/* Create new hashmap */
hashmap_t *hashmap_new(void);

/* Create hashmap with initial capacity */
hashmap_t *hashmap_with_capacity(size_t capacity);

/* Free hashmap and all entries */
void hashmap_free(hashmap_t *m);

/* Free hashmap (caller frees values) */
void hashmap_free_shell(hashmap_t *m);

/* Set key-value pair */
int hashmap_set(hashmap_t *m, const char *key, void *value);

/* Get value by key */
void *hashmap_get(hashmap_t *m, const char *key);

/* Check if key exists */
bool hashmap_has(hashmap_t *m, const char *key);

/* Remove key-value pair, return value */
void *hashmap_remove(hashmap_t *m, const char *key);

/* Get number of entries */
size_t hashmap_size(hashmap_t *m);

/* Clear all entries (caller frees values) */
void hashmap_clear(hashmap_t *m);

/* Get all keys (caller must free returned array) */
char **hashmap_keys(hashmap_t *m, size_t *out_count);

/* Get all values (caller must free returned array) */
void **hashmap_values(hashmap_t *m, size_t *out_count);

/* Iterate entries with callback */
void hashmap_foreach(hashmap_t *m,
                    void (*callback)(const char *key, void *value, void *ctx),
                    void *ctx);

#endif /* __HASHMAP_H__ */
