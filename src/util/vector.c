#include "vector.h"
#include <stdlib.h>
#include <string.h>

#define VECTOR_INITIAL_CAPACITY 16
#define VECTOR_GROWTH_FACTOR 1.5

vector_t *vector_new(void) {
    return vector_with_capacity(VECTOR_INITIAL_CAPACITY);
}

vector_t *vector_with_capacity(size_t capacity) {
    vector_t *v = malloc(sizeof(vector_t));
    if (!v) return NULL;

    v->items = malloc(capacity * sizeof(void *));
    if (!v->items) {
        free(v);
        return NULL;
    }

    v->count = 0;
    v->capacity = capacity;
    return v;
}

void vector_free(vector_t *v) {
    if (!v) return;
    for (size_t i = 0; i < v->count; i++) {
        free(v->items[i]);
    }
    free(v->items);
    free(v);
}

void vector_free_shell(vector_t *v) {
    if (!v) return;
    free(v->items);
    free(v);
}

static int vector_grow(vector_t *v) {
    size_t new_capacity = (size_t)(v->capacity * VECTOR_GROWTH_FACTOR);
    if (new_capacity == v->capacity) {
        new_capacity++;
    }

    void **new_items = realloc(v->items, new_capacity * sizeof(void *));
    if (!new_items) return -1;

    v->items = new_items;
    v->capacity = new_capacity;
    return 0;
}

int vector_append(vector_t *v, void *item) {
    if (!v) return -1;

    if (v->count >= v->capacity) {
        if (vector_grow(v) != 0) return -1;
    }

    v->items[v->count++] = item;
    return 0;
}

int vector_insert(vector_t *v, size_t index, void *item) {
    if (!v || index > v->count) return -1;

    if (v->count >= v->capacity) {
        if (vector_grow(v) != 0) return -1;
    }

    /* Shift items to the right */
    memmove(&v->items[index + 1], &v->items[index],
            (v->count - index) * sizeof(void *));

    v->items[index] = item;
    v->count++;
    return 0;
}

void *vector_remove(vector_t *v, size_t index) {
    if (!v || index >= v->count) return NULL;

    void *item = v->items[index];

    /* Shift items to the left */
    memmove(&v->items[index], &v->items[index + 1],
            (v->count - index - 1) * sizeof(void *));

    v->count--;
    return item;
}

void *vector_get(vector_t *v, size_t index) {
    if (!v || index >= v->count) return NULL;
    return v->items[index];
}

int vector_set(vector_t *v, size_t index, void *item) {
    if (!v || index >= v->count) return -1;
    v->items[index] = item;
    return 0;
}

size_t vector_length(vector_t *v) {
    return v ? v->count : 0;
}

bool vector_is_empty(vector_t *v) {
    return !v || v->count == 0;
}

void vector_clear(vector_t *v) {
    if (!v) return;
    v->count = 0;
}

int vector_find(vector_t *v, void *item) {
    if (!v) return -1;

    for (size_t i = 0; i < v->count; i++) {
        if (v->items[i] == item) {
            return (int)i;
        }
    }

    return -1;
}

int vector_find_with(vector_t *v, void *item, int (*cmp)(const void *a, const void *b)) {
    if (!v || !cmp) return -1;

    for (size_t i = 0; i < v->count; i++) {
        if (cmp(v->items[i], item) == 0) {
            return (int)i;
        }
    }

    return -1;
}

bool vector_contains(vector_t *v, void *item) {
    return vector_find(v, item) >= 0;
}

void vector_sort(vector_t *v, int (*cmp)(const void *a, const void *b)) {
    if (!v || !cmp || v->count < 2) return;

    qsort(v->items, v->count, sizeof(void *), cmp);
}
