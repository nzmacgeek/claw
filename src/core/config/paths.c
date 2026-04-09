#include "claw.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* Default installation paths */
static struct claw_paths _default_paths = {
    .config_dir = "/etc/claw",
    .state_dir  = "/var/lib/claw",
    .log_dir    = "/var/log/claw",
    .run_dir    = "/run/claw",
};

/*
 * Return the active runtime path configuration.
 *
 * If the CLAW_PREFIX environment variable is set, all paths are derived
 * from it so the daemon can be tested against a staged sysroot without
 * modifying the live filesystem:
 *
 *   CLAW_PREFIX=/tmp/test-sysroot claw
 *
 * gives:
 *   config_dir -> /tmp/test-sysroot/etc/claw
 *   state_dir  -> /tmp/test-sysroot/var/lib/claw
 *   log_dir    -> /tmp/test-sysroot/var/log/claw
 *   run_dir    -> /tmp/test-sysroot/run/claw
 *
 * The command-line -C flag in claw(8) always takes precedence over
 * CLAW_PREFIX for config_dir.
 */
struct claw_paths *claw_get_paths(void) {
    const char *prefix = getenv("CLAW_PREFIX");
    if (prefix && *prefix) {
        static struct claw_paths prefix_paths;
        static char config_dir[256];
        static char state_dir[256];
        static char log_dir[256];
        static char run_dir[256];

        snprintf(config_dir, sizeof(config_dir), "%s/etc/claw",      prefix);
        snprintf(state_dir,  sizeof(state_dir),  "%s/var/lib/claw",  prefix);
        snprintf(log_dir,    sizeof(log_dir),    "%s/var/log/claw",  prefix);
        snprintf(run_dir,    sizeof(run_dir),    "%s/run/claw",      prefix);

        prefix_paths.config_dir = config_dir;
        prefix_paths.state_dir  = state_dir;
        prefix_paths.log_dir    = log_dir;
        prefix_paths.run_dir    = run_dir;

        return &prefix_paths;
    }

    return &_default_paths;
}
