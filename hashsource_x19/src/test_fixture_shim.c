/*
 * Test Fixture Shim Library - LD_PRELOAD wrapper for single_board_test
 *
 * Features:
 * 1. Emulates /dev/bitmain-lcd (LCD driver)
 * 2. Emulates gpio943 (start button)
 * 3. Rewrites paths: /mnt/card/* -> /root/test_fixture/*

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
#include <limits.h>

/* ============================================================================
 * CONFIGURATION
 * ============================================================================ */

/* Debug logging */
#define LCD_DEBUG 1
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
        real_openat = dlsym(RTLD_NEXT, "openat");
        real_fopen = dlsym(RTLD_NEXT, "fopen");
        real_close = dlsym(RTLD_NEXT, "close");
        real_read = dlsym(RTLD_NEXT, "read");
        real_write = dlsym(RTLD_NEXT, "write");
        real_ioctl = dlsym(RTLD_NEXT, "ioctl");
        real_access = dlsym(RTLD_NEXT, "access");
        real_stat = dlsym(RTLD_NEXT, "stat");
        real_lstat = dlsym(RTLD_NEXT, "lstat");
        real_opendir = dlsym(RTLD_NEXT, "opendir");
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

/* Rewrite path from /mnt/card to /root/test_fixture */
static const char* rewrite_path(const char *path) {
    static char rewritten[PATH_MAX];

    if (!should_rewrite_path(path)) {
        return path;
    }

    snprintf(rewritten, sizeof(rewritten), "%s%s",
             REWRITE_PATH, path + strlen(ORIGINAL_PATH));

#if PATH_DEBUG
    fprintf(stderr, "[PATH REWRITE] %s -> %s\n", path, rewritten);
#endif

    return rewritten;
}

/* ============================================================================
 * DEVICE DETECTION
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

    /* For absolute paths, use open() which handles our interception */
    if (pathname && pathname[0] == '/') {
        if (flags & O_CREAT) {
            return open(pathname, flags, mode);
        } else {
            return open(pathname, flags);
        }
    }

    /* Relative paths pass through */
    pathname = rewrite_path(pathname);

    if (flags & O_CREAT) {
        return real_openat(dirfd, pathname, flags, mode);
    } else {
        return real_openat(dirfd, pathname, flags);
    }
}

/* Intercept fopen() */
FILE* fopen(const char *pathname, const char *mode) {
    init_real_functions();

    pathname = rewrite_path(pathname);
    return real_fopen(pathname, mode);
}

/* Intercept close() */
int close(int fd) {
    init_real_functions();

    /* Handle virtual FDs */
    if (fd == LCD_VIRTUAL_FD) {
        lcd_is_open = 0;
        return 0;
    }

    if (fd == GPIO_BUTTON_VIRTUAL_FD) {
        gpio_button_is_open = 0;
        return 0;
    }

    return real_close(fd);
}

/* Intercept read() */
ssize_t read(int fd, void *buf, size_t count) {
    init_real_functions();

    /* Handle GPIO button virtual FD */
    if (fd == GPIO_BUTTON_VIRTUAL_FD) {
        if (count >= 2) {
            ((char*)buf)[0] = '0';  /* Pressed */
            ((char*)buf)[1] = '\n';
            return 2;
        }
        return -1;
    }

    /* Real read */
    return real_read(fd, buf, count);
}

/* Intercept write() */
ssize_t write(int fd, const void *buf, size_t count) {
    init_real_functions();

    /* Handle LCD virtual FD */
    if (fd == LCD_VIRTUAL_FD) {
#if LCD_DEBUG
        fprintf(stderr, "[LCD SHIM] write(fd=%d, %zu bytes)\n", fd, count);
#endif
        return count;
    }

    /* Real write */
    return real_write(fd, buf, count);
}

/* Intercept ioctl() */
int ioctl(int fd, unsigned long request, ...) {
    va_list args;
    void *argp;

    init_real_functions();

    va_start(args, request);
    argp = va_arg(args, void*);
    va_end(args);

    /* Handle LCD virtual FD */
    if (fd == LCD_VIRTUAL_FD) {
#if LCD_DEBUG
        fprintf(stderr, "[LCD SHIM] ioctl(fd=%d, request=0x%lx)\n", fd, request);
#endif
        return 0;
    }

    /* Real ioctl */
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

/* Constructor - called when library is loaded */
__attribute__((constructor))
static void shim_init(void) {
    fprintf(stderr, "\n");
    fprintf(stderr, "================================================================================\n");
    fprintf(stderr, "  TEST FIXTURE SHIM - LD_PRELOAD Wrapper Loaded\n");
    fprintf(stderr, "================================================================================\n");
    fprintf(stderr, "  LCD Emulation:    /dev/bitmain-lcd -> virtual\n");
    fprintf(stderr, "  GPIO Emulation:   gpio943 (start button) -> auto-pressed\n");
    fprintf(stderr, "  Path Rewrite:     /mnt/card -> /root/test_fixture\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "================================================================================\n");
    fprintf(stderr, "\n");
}
