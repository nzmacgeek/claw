#ifndef __CONFIG_H__
#define __CONFIG_H__

#include "service.h"
#include "target.h"
#include "hashmap.h"

/* Loaded, validated configuration */
typedef struct {
    hashmap_t *services;        /* name → service_t* */
    hashmap_t *targets;         /* name → target_t* */
    char      *default_target;  /* Name of default boot target */
    char      *default_working_dir; /* Default cwd for service commands */
    char      *log_dir;
    log_level_t log_level;
} config_t;

/* Allocate empty config */
config_t *config_new(void);

/* Free config and all contained units */
void config_free(config_t *cfg);

/*
 * Load all unit files from a directory tree:
 *   <dir>/services.d/  (*.yml files)
 *   <dir>/targets.d/   (*.yml files)
 * and parse the main config file at <dir>/claw.conf.
 * Returns 0 on success, -1 on error (continues loading on individual file errors).
 */
int config_load(config_t *cfg, const char *dir);

/* Parse a single service YAML file into svc (pre-allocated).
 * Returns 0 on success, -1 on parse error. */
int config_parse_service(const char *path, service_t *svc);

/* Parse a single target YAML file into tgt (pre-allocated).
 * Returns 0 on success, -1 on parse error. */
int config_parse_target(const char *path, target_t *tgt);

#endif /* __CONFIG_H__ */
