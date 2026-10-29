/*
 * hvinit - tiny first-stage init for Galaxy A2 Core (a2corelte), H-VENDOR project.
 *
 * Why: the stock-style boot ramdisk carries an Android 9 init that cannot start an Android 11 GSI
 * (no APEX support). This stub does what Android 11's own first-stage init does, mounts /system,
 * /vendor and /odm from the device's partitions, then starts the GSI's Android 11 init:
 *     /system/bin/init selinux_setup
 * Every step is logged to /dev/kmsg with the prefix "hvinit:" so it shows up in /proc/last_kmsg.
 * On any hard failure it logs and reboots to the bootloader (download mode), like Android's init does.
 *
 * Build (static, arm64):  aarch64-linux-gnu-gcc -static -Os -o hvinit hvinit.c
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#include <linux/reboot.h>

#define AID_SYSTEM 1000
#define AID_READPROC 3009

static int kmsg_fd = -1;

static void klog(const char *fmt, ...) {
    char buf[300];
    int n = snprintf(buf, sizeof buf, "<6>hvinit: ");
    va_list ap;
    va_start(ap, fmt);
    n += vsnprintf(buf + n, sizeof buf - n - 2, fmt, ap);
    va_end(ap);
    if (n > (int)sizeof buf - 2) n = sizeof buf - 2;
    buf[n++] = '\n';
    if (kmsg_fd >= 0) { ssize_t r = write(kmsg_fd, buf, n); (void)r; }
}

static void die(const char *what) {
    klog("FATAL: %s (errno=%d %s) -> rebooting to download mode", what, errno, strerror(errno));
    sync();
    /* LINUX_REBOOT_CMD_RESTART2 with "download" - Samsung bootloader understands it, same as "bootloader" */
    syscall(SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2, LINUX_REBOOT_CMD_RESTART2, "download");
    for (;;) pause();
}

static void mk(const char *path, mode_t mode) {
    if (mkdir(path, mode) && errno != EEXIST) klog("mkdir %s failed: %s", path, strerror(errno));
}

static void node(const char *path, mode_t mode, int ma, int mi) {
    if (mknod(path, mode, makedev(ma, mi)) && errno != EEXIST)
        klog("mknod %s failed: %s", path, strerror(errno));
}

/* find /dev/mmcblk0pN for a GPT partition name (SYSTEM, VENDOR, ODM) via sysfs PARTNAME= */
static int find_part(const char *name, char *devpath, size_t len) {
    char p[160], line[160];
    for (int i = 1; i <= 40; i++) {
        snprintf(p, sizeof p, "/sys/block/mmcblk0/mmcblk0p%d/uevent", i);
        FILE *f = fopen(p, "r");
        if (!f) continue;
        int maj = -1, min = -1, hit = 0;
        while (fgets(line, sizeof line, f)) {
            line[strcspn(line, "\r\n")] = 0;
            if (!strncmp(line, "MAJOR=", 6)) maj = atoi(line + 6);
            else if (!strncmp(line, "MINOR=", 6)) min = atoi(line + 6);
            else if (!strncmp(line, "PARTNAME=", 9) && !strcasecmp(line + 9, name)) hit = 1;
        }
        fclose(f);
        if (hit && maj >= 0 && min >= 0) {
            snprintf(devpath, len, "/dev/mmcblk0p%d", i);
            node(devpath, S_IFBLK | 0600, maj, min);
            return 0;
        }
    }
    return -1;
}

static int mount_part(const char *part, const char *target, int required) {
    char dev[64];
    mk(target, 0755);
    if (find_part(part, dev, sizeof dev)) {
        klog("partition %s not found", part);
        if (required) die("missing partition");
        return -1;
    }
    if (mount(dev, target, "ext4", MS_RDONLY, NULL)) {
        klog("mount %s (%s) -> %s failed: %s", part, dev, target, strerror(errno));
        if (required) die("mount failed");
        return -1;
    }
    klog("mounted %s (%s) on %s (ro)", part, dev, target);
    return 0;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    umask(0);

    mount("tmpfs", "/dev", "tmpfs", MS_NOSUID, "mode=0755");
    node("/dev/kmsg", S_IFCHR | 0600, 1, 11);
    kmsg_fd = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
    klog("stage 1 start (Android 11 handoff stub)");

    node("/dev/null", S_IFCHR | 0666, 1, 3);
    node("/dev/zero", S_IFCHR | 0666, 1, 5);
    node("/dev/full", S_IFCHR | 0666, 1, 7);
    node("/dev/random", S_IFCHR | 0666, 1, 8);
    node("/dev/urandom", S_IFCHR | 0666, 1, 9);
    node("/dev/ptmx", S_IFCHR | 0666, 5, 2);
    node("/dev/tty", S_IFCHR | 0666, 5, 0);
    node("/dev/console", S_IFCHR | 0600, 5, 1);
    node("/dev/ttyprintk", S_IFCHR | 0660, 5, 3);

    mk("/dev/pts", 0755);
    mk("/dev/socket", 0755);
    mk("/dev/dm-user", 0755);
    mount("devpts", "/dev/pts", "devpts", 0, NULL);

    char opt[64];
    snprintf(opt, sizeof opt, "hidepid=2,gid=%d", AID_READPROC);
    mk("/proc", 0555);
    if (mount("proc", "/proc", "proc", 0, opt) && mount("proc", "/proc", "proc", 0, NULL)) klog("mount /proc failed");
    mk("/sys", 0555);
    if (mount("sysfs", "/sys", "sysfs", 0, NULL)) klog("mount /sys failed");

    /* same scratch mounts Android's first stage creates */
    mk("/mnt", 0755);
    mount("tmpfs", "/mnt", "tmpfs", MS_NOEXEC | MS_NOSUID | MS_NODEV, "mode=0755,uid=0,gid=1000");
    mk("/mnt/vendor", 0755);
    mk("/mnt/product", 0755);
    mk("/debug_ramdisk", 0755);
    mount("tmpfs", "/debug_ramdisk", "tmpfs", MS_NOEXEC | MS_NOSUID | MS_NODEV, "mode=0755");
    mk("/second_stage_resources", 0755);
    mount("tmpfs", "/second_stage_resources", "tmpfs", MS_NOEXEC | MS_NOSUID | MS_NODEV, "mode=0755");

    mount_part("ODM", "/odm", 0);
    mount_part("SYSTEM", "/system", 1);
    mount_part("VENDOR", "/vendor", 1);

    if (access("/system/bin/init", X_OK)) die("/system/bin/init missing or not executable");
    if (access("/system/bin/bootstrap/linker64", X_OK)) klog("WARNING: /system/bin/bootstrap/linker64 missing");

    klog("exec /system/bin/init selinux_setup");
    char *const args[] = {"/system/bin/init", "selinux_setup", NULL};
    execv("/system/bin/init", args);
    die("execv /system/bin/init");
    return 1;
}
