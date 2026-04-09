#include "claw.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* Default installation paths */
static const struct claw_paths _default_paths = {
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
const struct claw_paths *claw_get_paths(void) {
    const char *raw_prefix = getenv("CLAW_PREFIX");
    if (!raw_prefix || !*raw_prefix)
        return &_default_paths;

    /* Strip any trailing slashes so paths don't contain // */
    static char prefix_buf[4096];
    size_t plen = strlen(raw_prefix);
    if (plen >= sizeof(prefix_buf)) {
        fprintf(stderr,
                "[claw] CLAW_PREFIX too long (%zu bytes); ignoring and using defaults\n",
                plen);
        return &_default_paths;
    }
    memcpy(prefix_buf, raw_prefix, plen + 1);
    while (plen > 1 && prefix_buf[plen - 1] == '/')
        prefix_buf[--plen] = '\0';

    /* Suffixes appended to the prefix for each path */
    static const char s_config[] = "/etc/claw";
    static const char s_state[]  = "/var/lib/claw";
    static const char s_log[]    = "/var/log/claw";
    static const char s_run[]    = "/run/claw";

    /* Use a single static buffer large enough for the longest combination */
    static char config_dir[4096 + sizeof(s_config)];
    static char state_dir [4096 + sizeof(s_state)];
    static char log_dir   [4096 + sizeof(s_log)];
    static char run_dir   [4096 + sizeof(s_run)];

#define BUILD_PATH(buf, suffix) \
    do { \
        int _n = snprintf((buf), sizeof(buf), "%s%s", prefix_buf, (suffix)); \
        if (_n < 0 || (size_t)_n >= sizeof(buf)) { \
            fprintf(stderr, \
                    "[claw] path '%s%s' does not fit in buffer; using defaults\n", \
                    prefix_buf, (suffix)); \
            return &_default_paths; \
        } \
    } while (0)

    BUILD_PATH(config_dir, s_config);
    BUILD_PATH(state_dir,  s_state);
    BUILD_PATH(log_dir,    s_log);
    BUILD_PATH(run_dir,    s_run);

#undef BUILD_PATH

    static struct claw_paths prefix_paths;
    prefix_paths.config_dir = config_dir;
    prefix_paths.state_dir  = state_dir;
    prefix_paths.log_dir    = log_dir;
    prefix_paths.run_dir    = run_dir;

    return &prefix_paths;
}
