/*
 * FPGA Register Logger and Dump Tool
 * - Monitor mode: Restarts cgminer/bmminer and logs FPGA register changes
 * - Dump mode: One-time snapshot of all FPGA registers
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <time.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>

#define FPGA_DEVICE "/dev/axi_fpga_dev"
#define FPGA_SIZE 0x1200
#define NUM_REGS (FPGA_SIZE / 4)
#define POLL_INTERVAL_US  1000  // 1ms for faster capture

static volatile int g_running = 1;
static FILE *g_logfile = NULL;

void signal_handler(int signum) {
    (void)signum;
    g_running = 0;
}

void log_timestamp(FILE *f) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    fprintf(f, "[%ld.%06ld] ", ts.tv_sec, ts.tv_nsec / 1000);
}

int dump_fpga_registers(int show_all) {
    (void)show_all; // Unused parameter for now

    // Open FPGA device
    int fd = open(FPGA_DEVICE, O_RDWR | O_SYNC);
    if (fd < 0) {
        fprintf(stderr, "Error: Failed to open %s: %s\n", FPGA_DEVICE, strerror(errno));
        fprintf(stderr, "Are you running as root?\n");
        return 1;
    }

    // Memory map FPGA registers
    volatile uint32_t *regs = mmap(NULL, FPGA_SIZE, PROT_READ | PROT_WRITE,
                                   MAP_SHARED, fd, 0);
    if (regs == MAP_FAILED) {
        fprintf(stderr, "Error: Failed to mmap: %s\n", strerror(errno));
        close(fd);
        return 1;
    }

    printf("# FPGA Register Dump\n");
    printf("# Device: %s\n", FPGA_DEVICE);
    printf("# Size: 0x%03X (%d registers)\n", FPGA_SIZE, NUM_REGS);
    printf("# Format: OFFSET VALUE\n");
    printf("#\n\n");

    // Dump all registers
    int count = 0;
    for (uint32_t i = 0; i < NUM_REGS; i++) {
        uint32_t offset = i * 4;
        uint32_t value = regs[i];

        // Skip zero registers unless --all
        if (!show_all && value == 0) {
            continue;
        }

        printf("0x%03X: 0x%08X\n", offset, value);
        count++;
    }

    printf("\n# Total: %d registers displayed\n", count);

    // Cleanup
    munmap((void*)regs, FPGA_SIZE);
    close(fd);

    return 0;
}

int restart_cgminer(void) {
    printf("\n====================================\n");
    printf("Restarting cgminer/bmminer...\n");
    printf("====================================\n\n");

    // Kill existing processes
    system("killall -9 bmminer cgminer 2>/dev/null");
    sleep(2);

    // Restart via init script
    int ret = system("sudo /etc/init.d/S70cgminer restart");

    if (ret == 0) {
        printf("cgminer/bmminer restarted successfully\n");
        printf("Waiting 5 seconds before starting logger...\n\n");
        sleep(5);
        return 0;
    } else {
        fprintf(stderr, "Warning: Failed to restart cgminer (exit code %d)\n", ret);
        fprintf(stderr, "Continuing with logging anyway...\n\n");
        return -1;
    }
}

int main(int argc, char *argv[]) {
    const char *logfile = "/tmp/fpga_init.log";
    int auto_restart = 1;
    int dump_mode = 0;
    int show_all = 0;

    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--dump") == 0 || strcmp(argv[i], "-d") == 0) {
            dump_mode = 1;
        } else if (strcmp(argv[i], "--all") == 0 || strcmp(argv[i], "-a") == 0) {
            show_all = 1;
        } else if (strcmp(argv[i], "--no-restart") == 0) {
            auto_restart = 0;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("FPGA Register Logger and Dump Tool\n\n");
            printf("Usage: %s [mode] [options] [logfile]\n\n", argv[0]);
            printf("Modes:\n");
            printf("  (default)   Monitor mode - log ALL register changes\n");
            printf("  -d, --dump  Dump mode - one-time snapshot of all registers\n\n");
            printf("Monitor Mode Options:\n");
            printf("  --no-restart  Don't restart cgminer/bmminer before monitoring\n");
            printf("  <logfile>     Log file path (default: /tmp/fpga_init.log)\n\n");
            printf("Dump Mode Options:\n");
            printf("  -a, --all   Show all registers (default: only non-zero)\n\n");
            printf("Examples:\n");
            printf("  %s --dump              # Quick register dump (non-zero only)\n", argv[0]);
            printf("  %s --dump --all        # Full dump (all registers)\n", argv[0]);
            printf("  %s /tmp/my_init.log    # Monitor mode with custom log\n", argv[0]);
            printf("  %s --no-restart        # Monitor without restart\n\n", argv[0]);
            return 0;
        } else if (argv[i][0] != '-') {
            logfile = argv[i];
        }
    }

    // Dump mode - quick register snapshot
    if (dump_mode) {
        return dump_fpga_registers(show_all);
    }

    // Monitor mode
    printf("FPGA Register Change Logger with Auto-Restart\n");
    printf("==============================================\n");
    printf("Device: %s\n", FPGA_DEVICE);
    printf("Log file: %s\n", logfile);
    printf("Monitoring %d registers (0x000-0x%03X)\n", NUM_REGS, FPGA_SIZE - 4);
    printf("  - Includes: 0x000-0x03F, 0x040-0x07F, 0x080-0x0FF, etc.\n");
    printf("  - ALL registers are monitored for changes\n");
    printf("Poll interval: %d microseconds\n", POLL_INTERVAL_US);
    printf("Auto-restart: %s\n\n", auto_restart ? "yes" : "no");

    // Restart cgminer if requested
    if (auto_restart) {
        restart_cgminer();
    }

    printf("Press Ctrl+C to stop\n\n");

    // Setup signal handler
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Open FPGA device
    int fd = open(FPGA_DEVICE, O_RDWR | O_SYNC);
    if (fd < 0) {
        fprintf(stderr, "Failed to open %s: %s\n", FPGA_DEVICE, strerror(errno));
        fprintf(stderr, "Are you running as root?\n");
        return 1;
    }

    // Memory map FPGA registers
    volatile uint32_t *regs = mmap(NULL, FPGA_SIZE, PROT_READ | PROT_WRITE,
                                   MAP_SHARED, fd, 0);
    if (regs == MAP_FAILED) {
        fprintf(stderr, "Failed to mmap: %s\n", strerror(errno));
        close(fd);
        return 1;
    }

    printf("FPGA registers mapped at %p\n", (void*)regs);

    // Open log file
    g_logfile = fopen(logfile, "w");
    if (!g_logfile) {
        fprintf(stderr, "Failed to open log file: %s\n", strerror(errno));
        munmap((void*)regs, FPGA_SIZE);
        close(fd);
        return 1;
    }

    // Write header
    fprintf(g_logfile, "# FPGA Register Change Log\n");
    fprintf(g_logfile, "# Format: [timestamp] OFFSET OLD_VALUE NEW_VALUE\n");
    fprintf(g_logfile, "# Timestamp in seconds.microseconds since start\n\n");
    fflush(g_logfile);

    // Allocate shadow copy for change detection
    uint32_t *shadow = malloc(FPGA_SIZE);
    if (!shadow) {
        fprintf(stderr, "Failed to allocate memory\n");
        fclose(g_logfile);
        munmap((void*)regs, FPGA_SIZE);
        close(fd);
        return 1;
    }

    // Initialize shadow with current values
    memcpy(shadow, (void*)regs, FPGA_SIZE);

    // Log initial state (non-zero registers only)
    printf("Initial register state (non-zero):\n");
    fprintf(g_logfile, "# Initial State\n");

    for (uint32_t i = 0; i < NUM_REGS; i++) {
        uint32_t offset = i * 4;
        uint32_t value = regs[i];
        if (value != 0) {
            printf("  0x%03X = 0x%08X\n", offset, value);
            log_timestamp(g_logfile);
            fprintf(g_logfile, "INIT 0x%03X 0x%08X\n", offset, value);
        }
    }
    printf("\n");
    fflush(g_logfile);

    printf("Monitoring started...\n");

    // Main monitoring loop
    uint64_t poll_count = 0;
    uint64_t change_count = 0;

    while (g_running) {
        // Check all registers for changes
        for (uint32_t i = 0; i < NUM_REGS; i++) {
            uint32_t current = regs[i];
            uint32_t old = shadow[i];

            if (current != old) {
                uint32_t offset = i * 4;

                // Log to file when register value changes
                log_timestamp(g_logfile);
                fprintf(g_logfile, "0x%03X: 0x%08X -> 0x%08X\n", offset, old, current);
                fflush(g_logfile);

                // Log to console when register value changes
                log_timestamp(stdout);
                printf("0x%03X: 0x%08X -> 0x%08X\n", offset, old, current);

                // Update shadow
                shadow[i] = current;
                change_count++;
            }
        }

        poll_count++;

        // Print status every 10 seconds
        if (poll_count % (10000000 / POLL_INTERVAL_US) == 0) {
            printf("Status: %lu polls, %lu changes\n", poll_count, change_count);
        }

        usleep(POLL_INTERVAL_US);
    }

    printf("\nStopping...\n");
    printf("Total polls: %lu\n", poll_count);
    printf("Total changes: %lu\n", change_count);

    // Log final state (non-zero registers only)
    fprintf(g_logfile, "\n# Final State\n");
    for (uint32_t i = 0; i < NUM_REGS; i++) {
        uint32_t offset = i * 4;
        uint32_t value = regs[i];
        if (value != 0) {
            log_timestamp(g_logfile);
            fprintf(g_logfile, "FINAL 0x%03X 0x%08X\n", offset, value);
        }
    }

    // Cleanup
    free(shadow);
    fclose(g_logfile);
    munmap((void*)regs, FPGA_SIZE);
    close(fd);

    printf("Log saved to: %s\n", logfile);

    return 0;
}
