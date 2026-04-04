#ifndef __MEM_H__
#define __MEM_H__

#include <stddef.h>

/* Memory allocation with error checking (dies on failure) */
void *mem_alloc(size_t size);

/* Calloc with error checking */
void *mem_calloc(size_t count, size_t size);

/* Realloc with error checking */
void *mem_realloc(void *ptr, size_t size);

/* Free memory (safe for NULL) */
void mem_free(void *ptr);

/* Duplicate string with error checking */
char *mem_strdup(const char *str);

/* Duplicate n bytes with error checking */
void *mem_memdup(const void *src, size_t n);

#endif /* __MEM_H__ */
