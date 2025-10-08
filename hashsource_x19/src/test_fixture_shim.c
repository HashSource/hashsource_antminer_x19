/*
 * Test Fixture Shim Library - Comprehensive LD_PRELOAD wrapper
 *
 * Features:
 * 1. Emulates /dev/bitmain-lcd (LCD driver)
 * 2. Rewrites paths: /mnt/card/* -> /root/test_fixture/*
 *
 * This allows running single_board_test entirely from /root/test_fixture
 * without needing to copy files or create symlinks.
 *
 * Usage: LD_PRELOAD=./test_fixture_shim.so ./single_board_test
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>
#include <dlfcn.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>

/* ============================================================================
 * CONFIGURATION
 * ============================================================================ */

/* Debug logging - set to 1 to see LCD activity and path rewrites */
#define LCD_DEBUG 0
#define PATH_DEBUG 0

/* Virtual file descriptors */
#define LCD_VIRTUAL_FD 9999
#define GPIO_BUTTON_VIRTUAL_FD 9998

/* Path rewrite configuration */
#define ORIGINAL_PATH "/mnt/card"
#define REWRITE_PATH "/root/test_fixture"

/* ============================================================================
 * STATE TRACKING
 * ============================================================================ */

/* LCD state */
static int lcd_is_open = 0;
static char lcd_buffer[64]; /* 4 rows × 16 chars */

/* GPIO button state */
static int gpio_button_is_open = 0;

/* Function pointers to real syscalls */
static int (*real_open)(const char*, int, ...) = NULL;
static int (*real_openat)(int, const char*, int, ...) = NULL;
static FILE* (*real_fopen)(const char*, const char*) = NULL;
static int (*real_close)(int) = NULL;
static ssize_t (*real_read)(int, void*, size_t) = NULL;
static ssize_t (*real_write)(int, const void*, size_t) = NULL;
static int (*real_ioctl)(int, unsigned long, ...) = NULL;
static int (*real_access)(const char*, int) = NULL;
static int (*real_stat)(const char*, struct stat*) = NULL;
static int (*real_lstat)(const char*, struct stat*) = NULL;
static DIR* (*real_opendir)(const char*) = NULL;
static int (*real_mkdir)(const char*, mode_t) = NULL;

/* ============================================================================
 * INITIALIZATION
 * ============================================================================ */

/* Initialize function pointers on first use */
static void init_real_functions(void) {
    if (!real_open) {
        real_open = dlsym(RTLD_NEXT, "open");
    }
    if (!real_openat) {
        real_openat = dlsym(RTLD_NEXT, "openat");
    }
    if (!real_fopen) {
        real_fopen = dlsym(RTLD_NEXT, "fopen");
    }
    if (!real_close) {
        real_close = dlsym(RTLD_NEXT, "close");
    }
    if (!real_read) {
        real_read = dlsym(RTLD_NEXT, "read");
    }
    if (!real_write) {
        real_write = dlsym(RTLD_NEXT, "write");
    }
    if (!real_ioctl) {
        real_ioctl = dlsym(RTLD_NEXT, "ioctl");
    }
    if (!real_access) {
        real_access = dlsym(RTLD_NEXT, "access");
    }
    if (!real_stat) {
        real_stat = dlsym(RTLD_NEXT, "stat");
    }
    if (!real_lstat) {
        real_lstat = dlsym(RTLD_NEXT, "lstat");
    }
    if (!real_opendir) {
        real_opendir = dlsym(RTLD_NEXT, "opendir");
    }
    if (!real_mkdir) {
        real_mkdir = dlsym(RTLD_NEXT, "mkdir");
    }
}

/* ============================================================================
 * PATH REWRITING LOGIC
 * ============================================================================ */

/* Check if path should be rewritten */
static int should_rewrite_path(const char *path) {
    if (!path) return 0;
    return strncmp(path, ORIGINAL_PATH, strlen(ORIGINAL_PATH)) == 0;
}

/* Rewrite path from /mnt/card/* to /root/test_fixture/*
 * Returns pointer to static buffer - not thread safe but fine for our use case */
static const char* rewrite_path(const char *path) {
    static char rewritten[PATH_MAX];

    if (!should_rewrite_path(path)) {
        return path; /* No rewrite needed */
    }

    /* Build rewritten path */
    snprintf(rewritten, sizeof(rewritten), "%s%s",
             REWRITE_PATH, path + strlen(ORIGINAL_PATH));

#if PATH_DEBUG
    fprintf(stderr, "[PATH REWRITE] %s -> %s\n", path, rewritten);
#endif

    return rewritten;
}

/* ============================================================================
 * LCD DEVICE EMULATION
 * ============================================================================ */

/* Check if path is the LCD device */
static int is_lcd_device(const char *pathname) {
    return pathname && (
        strcmp(pathname, "/dev/bitmain-lcd") == 0 ||
        strstr(pathname, "bitmain-lcd") != NULL
    );
}

/* Check if path is the start button GPIO */
static int is_start_button(const char *pathname) {
    return pathname && (
        strcmp(pathname, "/sys/class/gpio/gpio943/value") == 0 ||
        strstr(pathname, "gpio943/value") != NULL
    );
}

/* ============================================================================
 * INTERCEPTED FUNCTIONS
 * ============================================================================ */

/* Intercept open() */
int open(const char *pathname, int flags, ...) {
    mode_t mode = 0;

    init_real_functions();

    /* Extract mode argument if O_CREAT is set */
    if (flags & O_CREAT) {
        va_list args;
        va_start(args, flags);
        mode = va_arg(args, mode_t);
        va_end(args);
    }

    /* Check if opening LCD device */
    if (is_lcd_device(pathname)) {
#if LCD_DEBUG
        fprintf(stderr, "[LCD SHIM] open(%s) intercepted\n", pathname);
#endif
        lcd_is_open = 1;
        memset(lcd_buffer, ' ', sizeof(lcd_buffer));
        return LCD_VIRTUAL_FD;
    }

    /* Check if opening GPIO start button */
    if (is_start_button(pathname)) {
#if LCD_DEBUG
        fprintf(stderr, "[GPIO SHIM] open(%s) intercepted - start button\n", pathname);
#endif
        gpio_button_is_open = 1;
        return GPIO_BUTTON_VIRTUAL_FD;
    }

    /* Apply path rewrite */
    pathname = rewrite_path(pathname);

    /* Pass through to real open */
    if (flags & O_CREAT) {
        return real_open(pathname, flags, mode);
    } else {
        return real_open(pathname, flags);
    }
}

/* Intercept open64() */
int open64(const char *pathname, int flags, ...) {
    mode_t mode = 0;

    if (flags & O_CREAT) {
        va_list args;
        va_start(args, flags);
        mode = va_arg(args, mode_t);
        va_end(args);
    }

    /* Redirect to our open() */
    if (flags & O_CREAT) {
        return open(pathname, flags, mode);
    } else {
        return open(pathname, flags);
    }
}

/* Intercept openat() */
int openat(int dirfd, const char *pathname, int flags, ...) {
    mode_t mode = 0;

    init_real_functions();

    if (flags & O_CREAT) {
        va_list args;
        va_start(args, flags);
        mode = va_arg(args, mode_t);
        va_end(args);
    }

    /* Apply path rewrite (only for absolute paths) */
    if (pathname && pathname[0] == '/') {
        pathname = rewrite_path(pathname);
    }

    /* Pass through */
    if (flags & O_CREAT) {
        return real_openat(dirfd, pathname, flags, mode);
    } else {
        return real_openat(dirfd, pathname, flags);
    }
}

/* Intercept fopen() */
FILE* fopen(const char *pathname, const char *mode) {
    init_real_functions();

    /* Apply path rewrite */
    pathname = rewrite_path(pathname);

    return real_fopen(pathname, mode);
}

/* Intercept fopen64() */
FILE* fopen64(const char *pathname, const char *mode) {
    return fopen(pathname, mode);
}

/* Intercept close() */
int close(int fd) {
    init_real_functions();

    if (fd == LCD_VIRTUAL_FD) {
#if LCD_DEBUG
        fprintf(stderr, "[LCD SHIM] close(%d) - LCD device closed\n", fd);
#endif
        lcd_is_open = 0;
        return 0;
    }

    if (fd == GPIO_BUTTON_VIRTUAL_FD) {
#if LCD_DEBUG
        fprintf(stderr, "[GPIO SHIM] close(%d) - GPIO button closed\n", fd);
#endif
        gpio_button_is_open = 0;
        return 0;
    }

    return real_close(fd);
}

/* Intercept read() */
ssize_t read(int fd, void *buf, size_t count) {
    init_real_functions();

    if (fd == GPIO_BUTTON_VIRTUAL_FD) {
        /* Return '0' to simulate button pressed (active-low) */
        if (buf && count > 0) {
            ((char*)buf)[0] = '0';
            if (count > 1) {
                ((char*)buf)[1] = '\n';
            }
#if LCD_DEBUG
            fprintf(stderr, "[GPIO SHIM] read(%d, %zu) - returning '0' (button pressed)\n", fd, count);
#endif
            return (count > 1) ? 2 : 1;
        }
        return 0;
    }

    return real_read(fd, buf, count);
}

/* Intercept write() */
ssize_t write(int fd, const void *buf, size_t count) {
    init_real_functions();

    if (fd == LCD_VIRTUAL_FD) {
        /* Emulate LCD driver behavior */
        size_t bytes_to_copy = count > sizeof(lcd_buffer) ? sizeof(lcd_buffer) : count;

        if (buf) {
            memcpy(lcd_buffer, buf, bytes_to_copy);
        }

#if LCD_DEBUG
        fprintf(stderr, "[LCD SHIM] write(%d, %zu bytes) - LCD content:\n", fd, count);
        fprintf(stderr, "  Row 0: %.16s\n", lcd_buffer);
        fprintf(stderr, "  Row 1: %.16s\n", lcd_buffer + 16);
        fprintf(stderr, "  Row 2: %.16s\n", lcd_buffer + 32);
        fprintf(stderr, "  Row 3: %.16s\n", lcd_buffer + 48);
#endif

        return count;
    }

    return real_write(fd, buf, count);
}

/* Intercept ioctl() */
int ioctl(int fd, unsigned long request, ...) {
    init_real_functions();

    if (fd == LCD_VIRTUAL_FD) {
#if LCD_DEBUG
        fprintf(stderr, "[LCD SHIM] ioctl(%d, 0x%lx) - not supported (returning 0)\n",
                fd, request);
#endif
        return 0;
    }

    /* Pass through to real ioctl */
    va_list args;
    va_start(args, request);
    void *argp = va_arg(args, void*);
    va_end(args);

    return real_ioctl(fd, request, argp);
}

/* Intercept access() */
int access(const char *pathname, int mode) {
    init_real_functions();
    pathname = rewrite_path(pathname);
    return real_access(pathname, mode);
}

/* Intercept stat() */
int stat(const char *pathname, struct stat *statbuf) {
    init_real_functions();
    pathname = rewrite_path(pathname);
    return real_stat(pathname, statbuf);
}

/* Intercept lstat() */
int lstat(const char *pathname, struct stat *statbuf) {
    init_real_functions();
    pathname = rewrite_path(pathname);
    return real_lstat(pathname, statbuf);
}

/* Note: stat64/lstat64 not intercepted - would conflict with system headers
 * The regular stat/lstat interception should handle most cases */

/* Intercept opendir() */
DIR* opendir(const char *name) {
    init_real_functions();
    name = rewrite_path(name);
    return real_opendir(name);
}

/* Intercept mkdir() */
int mkdir(const char *pathname, mode_t mode) {
    init_real_functions();
    pathname = rewrite_path(pathname);
    return real_mkdir(pathname, mode);
}

/* ============================================================================
 * LIBRARY INITIALIZATION
 * ============================================================================ */

/* Constructor - runs when library is loaded */
__attribute__((constructor))
static void test_fixture_shim_init(void) {
    fprintf(stderr, "[TEST FIXTURE SHIM] Loaded\n");
    fprintf(stderr, "  - LCD emulation: /dev/bitmain-lcd active\n");
    fprintf(stderr, "  - GPIO button emulation: gpio943 (auto-pressed)\n");
    fprintf(stderr, "  - Path rewrite: %s -> %s\n", ORIGINAL_PATH, REWRITE_PATH);
    init_real_functions();
}

/* Destructor - runs when library is unloaded */
__attribute__((destructor))
static void test_fixture_shim_fini(void) {
#if LCD_DEBUG || PATH_DEBUG
    fprintf(stderr, "[TEST FIXTURE SHIM] Unloaded\n");
#endif
}
