#define _GNU_SOURCE
/* MaixPy 4.12.5 camera process adapter for the V1.1 carrier's FPC2.
 * Camera discovery and sensor I/O open i2c-0 by default. FPC2 uses i2c-7,
 * and multimedia initialization resets A8/A9, so reapply mux on each open.
 * The supervising run_camera_v11.py restores the original pad registers. */
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

static int prepare_camera_i2c7(void)
{
    static volatile unsigned int *pads;
    if (pads == NULL) {
        int fd = (int)syscall(SYS_openat, AT_FDCWD, "/dev/mem", O_RDWR | O_SYNC, 0);
        if (fd < 0)
            return -1;
        void *mapping = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0x02300000);
        int saved_errno = errno;
        (void)syscall(SYS_close, fd);
        if (mapping == MAP_FAILED) {
            errno = saved_errno;
            return -1;
        }
        pads = mapping;
    }
    const unsigned int offsets[] = {0x6c / 4, 0x78 / 4};
    int changed = 0;
    for (unsigned int i = 0; i < 2; ++i) {
        unsigned int value = pads[offsets[i]];
        unsigned int wanted = (value & ~(7U << 16)) | (3U << 16);
        if (value != wanted) {
            pads[offsets[i]] = wanted;
            changed = 1;
        }
    }
    __sync_synchronize();
    if (changed) {
        static const char message[] = "CAMERA_I2C7_MUX_REAPPLIED\n";
        (void)syscall(SYS_write, STDERR_FILENO, message, sizeof(message) - 1);
    }
    return 0;
}

static const char *camera_path(const char *path)
{
    if (path != NULL && strcmp(path, "/dev/i2c-0") == 0) {
        static const char message[] = "CAMERA_I2C_ROUTE /dev/i2c-0 -> /dev/i2c-7\n";
        (void)syscall(SYS_write, STDERR_FILENO, message, sizeof(message) - 1);
        return "/dev/i2c-7";
    }
    return path;
}

static int file_mode_needed(int flags)
{
    return (flags & O_CREAT) || ((flags & O_TMPFILE) == O_TMPFILE);
}

static int routed_openat(int dirfd, const char *path, int flags, mode_t mode)
{
    const char *routed = camera_path(path);
    if (routed != path && prepare_camera_i2c7() != 0)
        return -1;
    return (int)syscall(SYS_openat, dirfd, routed, flags, mode);
}

#define DEFINE_OPEN(name) \
int name(const char *path, int flags, ...) \
{ \
    mode_t mode = 0; \
    if (file_mode_needed(flags)) { \
        va_list args; \
        va_start(args, flags); \
        mode = va_arg(args, mode_t); \
        va_end(args); \
    } \
    return routed_openat(AT_FDCWD, path, flags, mode); \
}

#define DEFINE_OPENAT(name) \
int name(int dirfd, const char *path, int flags, ...) \
{ \
    mode_t mode = 0; \
    if (file_mode_needed(flags)) { \
        va_list args; \
        va_start(args, flags); \
        mode = va_arg(args, mode_t); \
        va_end(args); \
    } \
    return routed_openat(dirfd, path, flags, mode); \
}

DEFINE_OPEN(open)
DEFINE_OPEN(open64)
DEFINE_OPENAT(openat)
DEFINE_OPENAT(openat64)
