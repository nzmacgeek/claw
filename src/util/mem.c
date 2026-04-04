#include "mem.h"
#include "log.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

void *mem_alloc(size_t size) {
    if (size == 0) {
        return NULL;
    }

    void *ptr = malloc(size);
    if (!ptr) {
        log_critical("memory", "Failed to allocate %zu bytes", size);
        exit(1);
    }

    return ptr;
}

void *mem_calloc(size_t count, size_t size) {
    if (count == 0 || size == 0) {
        return NULL;
    }

    void *ptr = calloc(count, size);
    if (!ptr) {
        log_critical("memory", "Failed to allocate %zu x %zu bytes", count, size);
        exit(1);
    }

    return ptr;
}

void *mem_realloc(void *ptr, size_t size) {
    if (size == 0) {
        mem_free(ptr);
        return NULL;
    }

    void *new_ptr = realloc(ptr, size);
    if (!new_ptr) {
        log_critical("memory", "Failed to reallocate %zu bytes", size);
        exit(1);
    }

    return new_ptr;
}

void mem_free(void *ptr) {
    if (ptr) {
        free(ptr);
    }
}

char *mem_strdup(const char *str) {
    if (!str) {
        return NULL;
    }

    size_t len = strlen(str);
    char *ptr = mem_alloc(len + 1);
    strcpy(ptr, str);
    return ptr;
}

void *mem_memdup(const void *src, size_t n) {
    if (!src || n == 0) {
        return NULL;
    }

    void *ptr = mem_alloc(n);
    memcpy(ptr, src, n);
    return ptr;
}
