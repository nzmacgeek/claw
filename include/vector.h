#ifndef __VECTOR_H__
#define __VECTOR_H__

#include <stddef.h>
#include <stdbool.h>

typedef struct {
    void **items;      /* Array of pointers */
    size_t count;      /* Current count */
    size_t capacity;   /* Allocated capacity */
} vector_t;

/* Create new vector */
vector_t *vector_new(void);

/* Create vector with initial capacity */
vector_t *vector_with_capacity(size_t capacity);

/* Free vector and contents */
void vector_free(vector_t *v);

/* Free vector (caller frees items) */
void vector_free_shell(vector_t *v);

/* Append item to vector */
int vector_append(vector_t *v, void *item);

/* Insert item at index */
int vector_insert(vector_t *v, size_t index, void *item);

/* Remove and return item at index */
void *vector_remove(vector_t *v, size_t index);

/* Get item at index */
void *vector_get(vector_t *v, size_t index);

/* Set item at index */
int vector_set(vector_t *v, size_t index, void *item);

/* Get vector length */
size_t vector_length(vector_t *v);

/* Check if vector is empty */
bool vector_is_empty(vector_t *v);

/* Clear all items (caller frees) */
void vector_clear(vector_t *v);

/* Find index of item (pointer equality) */
int vector_find(vector_t *v, void *item);

/* Find index using comparison function */
int vector_find_with(vector_t *v, void *item, int (*cmp)(const void *a, const void *b));

/* Check if vector contains item */
bool vector_contains(vector_t *v, void *item);

/* Sort vector in place */
void vector_sort(vector_t *v, int (*cmp)(const void *a, const void *b));

#endif /* __VECTOR_H__ */
