#include "claw-string.h"
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <stdio.h>

char *string_trim(const char *str) {
    if (!str) return NULL;

    /* Skip leading whitespace */
    while (*str && isspace(*str)) {
        str++;
    }

    size_t len = strlen(str);
    if (len == 0) {
        char *result = malloc(1);
        if (result) result[0] = '\0';
        return result;
    }

    /* Find end, skipping trailing whitespace */
    while (len > 0 && isspace(str[len - 1])) {
        len--;
    }

    char *result = malloc(len + 1);
    if (result) {
        strncpy(result, str, len);
        result[len] = '\0';
    }

    return result;
}

char **string_split(const char *str, const char *delim, int *out_count) {
    if (!str || !delim) {
        if (out_count) *out_count = 0;
        return NULL;
    }

    char *copy = malloc(strlen(str) + 1);
    if (!copy) return NULL;
    strcpy(copy, str);

    /* Count occurrences */
    int count = 1;
    char *p = copy;
    while ((p = strstr(p, delim))) {
        count++;
        p += strlen(delim);
    }

    char **result = malloc(count * sizeof(char *));
    if (!result) {
        free(copy);
        return NULL;
    }

    strcpy(copy, str);
    p = copy;
    int idx = 0;

    for (int i = 0; i < count; i++) {
        char *next = strstr(p, delim);
        if (next) {
            *next = '\0';
            result[idx++] = string_dup(p);
            p = next + strlen(delim);
        } else {
            result[idx++] = string_dup(p);
        }
    }

    free(copy);
    if (out_count) *out_count = idx;
    return result;
}

char *string_join(char **array, int count, const char *delim) {
    if (!array || count <= 0) return NULL;

    size_t total = 0;
    for (int i = 0; i < count; i++) {
        total += strlen(array[i]);
        if (i < count - 1) total += strlen(delim);
    }

    char *result = malloc(total + 1);
    if (!result) return NULL;

    result[0] = '\0';
    for (int i = 0; i < count; i++) {
        strcat(result, array[i]);
        if (i < count - 1) strcat(result, delim);
    }

    return result;
}

bool string_contains(const char *str, const char *substr) {
    if (!str || !substr) return false;
    return strstr(str, substr) != NULL;
}

bool string_starts_with(const char *str, const char *prefix) {
    if (!str || !prefix) return false;
    return strncmp(str, prefix, strlen(prefix)) == 0;
}

bool string_ends_with(const char *str, const char *suffix) {
    if (!str || !suffix) return false;

    size_t str_len = strlen(str);
    size_t suffix_len = strlen(suffix);

    if (str_len < suffix_len) return false;

    return strcmp(str + str_len - suffix_len, suffix) == 0;
}

int string_case_cmp(const char *a, const char *b) {
    if (!a || !b) return -1;

    while (*a && *b) {
        int ca = tolower(*a);
        int cb = tolower(*b);
        if (ca != cb) return ca - cb;
        a++;
        b++;
    }

    return tolower(*a) - tolower(*b);
}

char *string_dup(const char *str) {
    if (!str) return NULL;

    char *result = malloc(strlen(str) + 1);
    if (result) {
        strcpy(result, str);
    }

    return result;
}

char *string_ndup(const char *str, size_t n) {
    if (!str) return NULL;

    size_t len = strlen(str);
    if (len > n) len = n;

    char *result = malloc(len + 1);
    if (result) {
        strncpy(result, str, len);
        result[len] = '\0';
    }

    return result;
}

char *string_to_lower(const char *str) {
    if (!str) return NULL;

    char *result = malloc(strlen(str) + 1);
    if (!result) return NULL;

    for (size_t i = 0; str[i]; i++) {
        result[i] = tolower(str[i]);
    }
    result[strlen(str)] = '\0';

    return result;
}

char *string_to_upper(const char *str) {
    if (!str) return NULL;

    char *result = malloc(strlen(str) + 1);
    if (!result) return NULL;

    for (size_t i = 0; str[i]; i++) {
        result[i] = toupper(str[i]);
    }
    result[strlen(str)] = '\0';

    return result;
}

char *string_replace(const char *str, const char *old, const char *new) {
    if (!str || !old || !new) return NULL;

    char *pos = strstr(str, old);
    if (!pos) {
        return string_dup(str);
    }

    size_t total = strlen(str) - strlen(old) + strlen(new) + 1;
    char *result = malloc(total);
    if (!result) return NULL;

    size_t offset = pos - str;
    strncpy(result, str, offset);
    strcpy(result + offset, new);
    strcat(result, str + offset + strlen(old));

    return result;
}

char *string_replace_all(const char *str, const char *old, const char *new) {
    if (!str || !old || !new || strlen(old) == 0) return NULL;

    char *result = string_dup(str);
    if (!result) return NULL;

    char *pos = result;
    while ((pos = strstr(pos, old))) {
        char *replaced = string_replace(result, old, new);
        free(result);
        result = replaced;
        pos = result + (pos - result) + strlen(new);
    }

    return result;
}

char *string_substitute(const char *str) {
    if (!str) return NULL;

    char result[2048] = {0};
    size_t out_pos = 0;
    const char *in_pos = str;

    while (*in_pos && out_pos < sizeof(result) - 1) {
        if (*in_pos == '$' && *(in_pos + 1) == '{') {
            /* Variable substitution */
            in_pos += 2;

            /* Extract variable name */
            char varname[256] = {0};
            char *vp = varname;
            int default_start = -1;

            while (*in_pos && *in_pos != '}' && vp - varname < 255) {
                if (*in_pos == ':' && *(in_pos + 1) == '-') {
                    default_start = in_pos - str;
                    in_pos += 2;
                    break;
                }
                *vp++ = *in_pos++;
            }
            *vp = '\0';

            /* Get default value if specified */
            char default_val[256] = {0};
            if (default_start >= 0) {
                char *dp = default_val;
                while (*in_pos && *in_pos != '}' && dp - default_val < 255) {
                    *dp++ = *in_pos++;
                }
                *dp = '\0';
            }

            /* Get environment variable */
            const char *env_val = getenv(varname);
            const char *value = env_val ? env_val : default_val;

            size_t vlen = strlen(value);
            if (out_pos + vlen < sizeof(result) - 1) {
                strcpy(result + out_pos, value);
                out_pos += vlen;
            }

            if (*in_pos == '}') in_pos++;
        } else {
            result[out_pos++] = *in_pos++;
        }
    }

    result[out_pos] = '\0';
    return string_dup(result);
}

char **string_parse_kv_pairs(const char *str, int *out_count) {
    if (!str) {
        if (out_count) *out_count = 0;
        return NULL;
    }

    /* Count items */
    int count = 0;
    const char *p = str;
    bool in_item = false;

    while (*p) {
        if (!isspace(*p)) {
            if (!in_item) {
                count++;
                in_item = true;
            }
        } else {
            in_item = false;
        }
        p++;
    }

    if (count == 0) {
        if (out_count) *out_count = 0;
        return NULL;
    }

    char **result = malloc(count * sizeof(char *));
    if (!result) return NULL;

    char *copy = malloc(strlen(str) + 1);
    if (!copy) {
        free(result);
        return NULL;
    }

    strcpy(copy, str);

    int idx = 0;
    p = strtok(copy, " \t\n\r");
    while (p && idx < count) {
        result[idx++] = string_dup(p);
        p = strtok(NULL, " \t\n\r");
    }

    free(copy);
    if (out_count) *out_count = idx;
    return result;
}

const char *string_get_kv_value(const char *pairs, const char *key) {
    if (!pairs || !key) return NULL;

    char *copy = malloc(strlen(pairs) + 1);
    if (!copy) return NULL;

    strcpy(copy, pairs);
    char *line = strtok(copy, "\n");

    while (line) {
        char *eq = strchr(line, '=');
        if (eq) {
            *eq = '\0';
            if (strcmp(line, key) == 0) {
                const char *value = eq + 1;
                free(copy);
                return value;  /* Warning: points into freed memory */
            }
        }
        line = strtok(NULL, "\n");
    }

    free(copy);
    return NULL;
}
