#ifndef __OS_H__
#define __OS_H__

/*
 * os.h — Early-boot OS setup (PID 1 responsibilities).
 *
 * These functions run before service activation and prepare the kernel
 * filesystem interfaces and minimal runtime directories.
 */

/* Mount all essential pseudo-filesystems:
 *   /proc  (procfs)
 *   /sys   (sysfs)
 *   /dev   (devtmpfs with fallback to tmpfs + minimal nodes)
 *   /run   (tmpfs)
 *
 * Silently no-ops if already mounted.
 * Returns 0 on success, -1 if any critical mount failed.
 */
int os_mount_early(void);

/* Create required runtime directories under /run if they don't exist.
 * Creates: /run/claw, /run/lock, /run/log
 * Returns 0 on success.
 */
int os_create_runtime_dirs(void);

/* Set hostname from /etc/hostname (or "blueyos" as default).
 * Returns 0 on success, -1 on failure.
 */
int os_set_hostname(void);

/* Seed the kernel PRNG from /var/lib/claw/random.seed if available.
 * After reading, refreshes the seed file with new random bytes.
 * Non-fatal if the seed file is missing.
 */
void os_seed_random(void);

/* Mount all entries from /etc/fstab that have the 'auto' or no opts field.
 * Called after root FS check completes.
 * Returns number of successful mounts.
 */
int os_mount_fstab(void);

#endif /* __OS_H__ */
