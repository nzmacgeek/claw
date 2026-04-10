/* Linux-specific headers need GNU source for sethostname, mknod, etc. */
#define _GNU_SOURCE
#include "os.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mount.h>
#include <sys/sysmacros.h>

static int build_prefixed_path(char *buf, size_t size, const char *suffix) {
    const char *prefix = getenv("CLAW_PREFIX");
    char prefix_buf[4096];
    size_t plen;
    int n;

    if (!prefix || !*prefix) {
        n = snprintf(buf, size, "%s", suffix);
        return (n < 0 || (size_t)n >= size) ? -1 : 0;
    }

    plen = strlen(prefix);
    if (plen >= sizeof(prefix_buf))
        return -1;

    memcpy(prefix_buf, prefix, plen + 1);
    while (plen > 1 && prefix_buf[plen - 1] == '/')
        prefix_buf[--plen] = '\0';

    n = snprintf(buf, size, "%s%s", prefix_buf, suffix);
    return (n < 0 || (size_t)n >= size) ? -1 : 0;
}

/* -----------------------------------------------------------------------
 * Helpers
 * --------------------------------------------------------------------- */

/* Try to mount a filesystem; tolerate EBUSY (already mounted). */
static int try_mount(const char *source, const char *target,
                     const char *fstype, unsigned long flags,
                     const char *opts) {
    if (mount(source, target, fstype, flags, opts) == 0) {
        log_debug("os", "Mounted %s on %s (%s)", source, target, fstype);
        return 0;
    }
    if (errno == EBUSY) {
        log_debug("os", "%s already mounted", target);
        return 0;  /* Already mounted — that's fine */
    }
    log_warning("os", "mount(%s) failed: %s", target, strerror(errno));
    return -1;
}

/* Create directory if it does not exist already */
static void ensure_dir(const char *path, mode_t mode) {
    char buf[512];
    size_t len;

    if (!path || !*path) return;

    len = strlen(path);
    if (len >= sizeof(buf)) {
        log_debug("os", "mkdir %s: path too long", path);
        return;
    }

    strcpy(buf, path);
    for (char *p = buf + 1; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        if (mkdir(buf, 0755) != 0 && errno != EEXIST) {
            log_debug("os", "mkdir %s: %s", buf, strerror(errno));
            *p = '/';
            return;
        }
        *p = '/';
    }

    if (mkdir(buf, mode) != 0 && errno != EEXIST) {
        log_debug("os", "mkdir %s: %s", path, strerror(errno));
    }
}

/* Create a device node if it does not exist */
static void ensure_dev(const char *path, mode_t type, int major, int minor) {
    struct stat st;
    if (stat(path, &st) == 0) return;  /* Already exists */
    if (mknod(path, type | 0600, makedev(major, minor)) != 0) {
        log_debug("os", "mknod %s: %s", path, strerror(errno));
    }
}

/* -----------------------------------------------------------------------
 * os_mount_early
 * --------------------------------------------------------------------- */

int os_mount_early(void) {
    int rc = 0;

    /* /proc — required by many utilities and the kernel itself
     * Mount flags: nosuid, noexec, nodev */
    ensure_dir("/proc", 0555);
    if (try_mount("proc", "/proc", "proc",
                  MS_NOSUID | MS_NOEXEC | MS_NODEV, NULL) != 0) {
        log_error("os", "Failed to mount /proc (critical)");
        rc = -1;
    }

    /* /sys */
    ensure_dir("/sys", 0555);
    if (try_mount("sysfs", "/sys", "sysfs",
                  MS_NOSUID | MS_NOEXEC | MS_NODEV, NULL) != 0) {
        log_warning("os", "Failed to mount /sys");
    }

    /* /sys/fs/cgroup (optional, best-effort) */
    ensure_dir("/sys/fs/cgroup", 0555);
    try_mount("cgroup2", "/sys/fs/cgroup", "cgroup2",
              MS_NOSUID | MS_NOEXEC | MS_RELATIME, NULL);

    /* /dev — prefer devtmpfs (populated by kernel); fall back to tmpfs */
    ensure_dir("/dev", 0755);
    if (try_mount("devtmpfs", "/dev", "devtmpfs", MS_NOSUID, NULL) != 0) {
        log_info("os", "devtmpfs not available, using tmpfs for /dev");
        if (try_mount("tmpfs", "/dev", "tmpfs",
                      MS_NOSUID, "mode=0755,size=10M") != 0) {
            log_error("os", "Failed to mount /dev (critical)");
            rc = -1;
        } else {
            /* Create minimal device nodes needed for early boot */
            ensure_dev("/dev/null",    S_IFCHR, 1, 3);
            ensure_dev("/dev/zero",    S_IFCHR, 1, 5);
            ensure_dev("/dev/random",  S_IFCHR, 1, 8);
            ensure_dev("/dev/urandom", S_IFCHR, 1, 9);
            ensure_dev("/dev/tty",     S_IFCHR, 5, 0);
            ensure_dev("/dev/console", S_IFCHR, 5, 1);
            ensure_dev("/dev/ptmx",    S_IFCHR, 5, 2);
            ensure_dir("/dev/pts", 0755);
            ensure_dir("/dev/shm", 01777);
        }
    }

    /* /dev/pts — devpts for pseudo-terminals */
    ensure_dir("/dev/pts", 0755);
    try_mount("devpts", "/dev/pts", "devpts",
              MS_NOSUID | MS_NOEXEC, "mode=0620,gid=5,ptmxmode=0666");

    /* /dev/shm — POSIX shared memory */
    ensure_dir("/dev/shm", 01777);
    /* Only mount if not already a bind of tmpfs */
    {
        struct stat st;
        if (stat("/dev/shm", &st) == 0 && !S_ISDIR(st.st_mode)) {
            /* something weird — skip */
        } else {
            try_mount("tmpfs", "/dev/shm", "tmpfs",
                      MS_NOSUID | MS_NODEV, "mode=01777");
        }
    }

    /* /run — volatile runtime state (tmpfs) */
    ensure_dir("/run", 0755);
    if (try_mount("tmpfs", "/run", "tmpfs",
                  MS_NOSUID | MS_NODEV, "mode=0755,size=20%") != 0) {
        log_warning("os", "Failed to mount /run tmpfs — using existing directory");
    }

    return rc;
}

/* -----------------------------------------------------------------------
 * os_create_runtime_dirs
 * --------------------------------------------------------------------- */

int os_create_runtime_dirs(void) {
    const struct claw_paths *paths = claw_get_paths();
    char run_lock[512];
    char run_log[512];

    if (build_prefixed_path(run_lock, sizeof(run_lock), "/run/lock") != 0)
        strcpy(run_lock, "/run/lock");
    if (build_prefixed_path(run_log, sizeof(run_log), "/run/log") != 0)
        strcpy(run_log, "/run/log");

    ensure_dir(paths->run_dir, 0700);  /* IPC socket — root only */
    ensure_dir(run_lock, 0755);
    ensure_dir(run_log, 0755);
    ensure_dir(paths->log_dir, 0750);
    ensure_dir(paths->state_dir, 0700);
    return 0;
}

/* -----------------------------------------------------------------------
 * os_set_hostname
 * --------------------------------------------------------------------- */

int os_set_hostname(void) {
    char hostname[256] = "blueyos";

    FILE *f = fopen("/etc/hostname", "r");
    if (f) {
        if (fgets(hostname, (int)sizeof(hostname), f)) {
            /* Trim trailing newline */
            size_t len = strlen(hostname);
            while (len > 0 && (hostname[len-1] == '\n' || hostname[len-1] == '\r'))
                hostname[--len] = '\0';
        }
        fclose(f);
    }

    if (sethostname(hostname, strlen(hostname)) != 0) {
        log_warning("os", "sethostname(%s) failed: %s", hostname, strerror(errno));
        return -1;
    }

    log_info("os", "Hostname set to: %s", hostname);
    return 0;
}

/* -----------------------------------------------------------------------
 * os_seed_random
 * --------------------------------------------------------------------- */

void os_seed_random(void) {
    const struct claw_paths *paths = claw_get_paths();
    char buf[512];
    char seed_file[512];

    if (snprintf(seed_file, sizeof(seed_file), "%s/random.seed", paths->state_dir)
        >= (int)sizeof(seed_file)) {
        log_warning("os", "Random seed path is too long: %s/random.seed", paths->state_dir);
        return;
    }

    int fd = open(seed_file, O_RDONLY);
    if (fd < 0) {
        log_debug("os", "No random seed file at %s", seed_file);
    } else {
        ssize_t n = read(fd, buf, sizeof(buf));
        close(fd);

        if (n > 0) {
            /* Write seed into kernel entropy pool via /dev/urandom */
            int urandom = open("/dev/urandom", O_WRONLY);
            if (urandom >= 0) {
                ssize_t written = write(urandom, buf, (size_t)n);
                (void)written;
                close(urandom);
                log_info("os", "Seeded kernel PRNG from %s (%zd bytes)", seed_file, n);
            }
        }
    }

    ensure_dir(paths->state_dir, 0700);

    /* Refresh the seed file with new random bytes */
    int seed_out = open(seed_file, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (seed_out >= 0) {
        int urandom2 = open("/dev/urandom", O_RDONLY);
        if (urandom2 >= 0) {
            ssize_t rn = read(urandom2, buf, sizeof(buf));
            if (rn > 0) {
                ssize_t wn = write(seed_out, buf, (size_t)rn);
                (void)wn;
            }
            close(urandom2);
        }
        close(seed_out);
    }
}

/* -----------------------------------------------------------------------
 * os_mount_fstab — Parse /etc/fstab and mount auto-mount entries
 * --------------------------------------------------------------------- */

/* fstab field indices */
#define FSTAB_DEV    0
#define FSTAB_MPOINT 1
#define FSTAB_FSTYPE 2
#define FSTAB_OPTS   3
#define FSTAB_DUMP   4
#define FSTAB_PASS   5
#define FSTAB_FIELDS 6

int os_mount_fstab(void) {
    FILE *f = fopen("/etc/fstab", "r");
    if (!f) {
        log_debug("os", "/etc/fstab not found");
        return 0;
    }

    char line[512];
    int mounted = 0;

    while (fgets(line, (int)sizeof(line), f)) {
        /* Skip comments and blank lines */
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == '\0') continue;

        /* Split into fields */
        char *fields[FSTAB_FIELDS];
        int fi = 0;
        char *tok = strtok(p, " \t\n");
        while (tok && fi < FSTAB_FIELDS) {
            fields[fi++] = tok;
            tok = strtok(NULL, " \t\n");
        }
        if (fi < 4) continue;  /* Malformed line */

        const char *dev    = fields[FSTAB_DEV];
        const char *mpoint = fields[FSTAB_MPOINT];
        const char *fstype = fields[FSTAB_FSTYPE];
        const char *opts   = fi > 3 ? fields[FSTAB_OPTS] : "defaults";

        /* Skip noauto entries and already-handled pseudo-fs */
        if (strstr(opts, "noauto")) continue;
        if (strcmp(mpoint, "/proc")    == 0) continue;
        if (strcmp(mpoint, "/sys")     == 0) continue;
        if (strcmp(mpoint, "/dev")     == 0) continue;
        if (strcmp(mpoint, "/run")     == 0) continue;
        if (strcmp(mpoint, "none")     == 0) continue;
        if (strcmp(mpoint, "swap")     == 0) continue;

        /* Build mount flags from options string */
        unsigned long flags = MS_RELATIME;
        if (strstr(opts, "ro"))     flags |= MS_RDONLY;
        if (strstr(opts, "nosuid")) flags |= MS_NOSUID;
        if (strstr(opts, "nodev"))  flags |= MS_NODEV;
        if (strstr(opts, "noexec")) flags |= MS_NOEXEC;
        if (strstr(opts, "bind"))   flags |= MS_BIND;

        ensure_dir(mpoint, 0755);

        if (try_mount(dev, mpoint, fstype, flags, opts) == 0)
            mounted++;
    }

    fclose(f);
    log_info("os", "fstab: %d entry(ies) mounted", mounted);
    return mounted;
}
