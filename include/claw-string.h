#ifndef __CLAW_STRING_H__
#define __CLAW_STRING_H__

#include <stddef.h>
#include <stdbool.h>

/* Trim whitespace from both ends */
char *string_trim(const char *str);

/* Split string by delimiter (returned array must be freed by caller) */
char **string_split(const char *str, const char *delim, int *out_count);

/* Join array with delimiter */
char *string_join(char **array, int count, const char *delim);

/* String contains substring */
bool string_contains(const char *str, const char *substr);

/* String starts with prefix */
bool string_starts_with(const char *str, const char *prefix);

/* String ends with suffix */
bool string_ends_with(const char *str, const char *suffix);

/* Case-insensitive comparison */
int string_case_cmp(const char *a, const char *b);

/* Duplicate string */
char *string_dup(const char *str);

/* Duplicate n characters from string */
char *string_ndup(const char *str, size_t n);

/* Convert string to lowercase */
char *string_to_lower(const char *str);

/* Convert string to uppercase */
char *string_to_upper(const char *str);

/* Replace first occurrence of old with new (returned string must be freed) */
char *string_replace(const char *str, const char *old, const char *new);

/* Replace all occurrences of old with new */
char *string_replace_all(const char *str, const char *old, const char *new);

/* Perform variable substitution ${VAR} and ${VAR:-default} */
char *string_substitute(const char *str);

/* Parse key=value pairs from string */
char **string_parse_kv_pairs(const char *str, int *out_count);

/* Get value from newline-separated key=value pairs */
const char *string_get_kv_value(const char *pairs, const char *key);

#endif /* __CLAW_STRING_H__ */
