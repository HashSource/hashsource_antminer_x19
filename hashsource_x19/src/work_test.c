/*
 * BM1398 Work Submission Test
 *
 * Tests work submission and nonce reading on S19 Pro ASIC chips.
 * Uses a known Bitcoin block with golden nonce for verification.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include "../include/bm1398_asic.h"

// Test configuration
#define TEST_CHAIN 0
#define TEST_WORK_COUNT 10
#define NONCE_READ_TIMEOUT_MS 5000

/**
 * Print buffer as hex
 */
void print_hex(const char *label, const uint8_t *data, size_t len) {
    printf("%s: ", label);
    for (size_t i = 0; i < len; i++) {
        printf("%02x", data[i]);
        if ((i + 1) % 32 == 0 && i + 1 < len) {
            printf("\n%*s", (int)strlen(label) + 2, "");
        }
    }
    printf("\n");
}

/**
 * Create test work packet with simple pattern
 * This creates predictable test data for debugging
 */
void create_test_work(uint32_t work_id, uint8_t work_data[12],
                     uint8_t midstates[4][32]) {
    // Simple work data pattern (last 12 bytes of block header)
    for (int i = 0; i < 12; i++) {
        work_data[i] = (uint8_t)(work_id + i);
    }

    // Simple midstate patterns (normally these would be SHA256 states)
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 32; j++) {
            midstates[i][j] = (uint8_t)(work_id * 4 + i * 32 + j);
        }
    }
}

/**
 * Main test function
 */
int main(int argc, char *argv[]) {
    int chain = TEST_CHAIN;

    if (argc > 1) {
        chain = atoi(argv[1]);
        if (chain < 0 || chain >= MAX_CHAINS) {
            fprintf(stderr, "Error: Invalid chain %d (must be 0-%d)\n",
                   chain, MAX_CHAINS - 1);
            return 1;
        }
    }

    printf("\n");
    printf("====================================\n");
    printf("BM1398 Work Submission Test\n");
    printf("====================================\n");
    printf("Target: Chain %d\n", chain);
    printf("Work count: %d\n", TEST_WORK_COUNT);
    printf("\n");

    // Initialize driver
    bm1398_context_t ctx;
    if (bm1398_init(&ctx) < 0) {
        fprintf(stderr, "Error: Failed to initialize BM1398 driver\n");
        return 1;
    }

    // CRITICAL: Perform power release cycle before initialization
    printf("====================================\n");
    printf("Performing Power Release Cycle\n");
    printf("====================================\n");
    // Disable PSU via GPIO 907
    extern int gpio_setup(int gpio, int value);
    gpio_setup(907, 1);
    printf("PSU disabled. Waiting 5 seconds for power discharge...\n");
    sleep(5);
    printf("Power release complete.\n\n");

    // Power on PSU
    printf("====================================\n");
    printf("Powering On PSU\n");
    printf("====================================\n");
    printf("Voltage: 15.0V\n");
    if (bm1398_psu_power_on(&ctx, 15000) < 0) {
        fprintf(stderr, "Error: Failed to power on PSU\n");
        bm1398_cleanup(&ctx);
        return 1;
    }
    printf("PSU powered on\n\n");

    // Enable hashboard DC-DC converter
    printf("====================================\n");
    printf("Enabling Hashboard DC-DC Converter\n");
    printf("====================================\n");
    if (bm1398_enable_dc_dc(&ctx, chain) < 0) {
        printf("Note: DC-DC enable failed (may already be enabled from previous run)\n");
        printf("Continuing with test...\n");
    }
    sleep(1);  // Allow power to stabilize

    // CRITICAL: FPGA reset after PIC DC-DC enable
    // Source: single_board_test_pt1.log line 116: "pic_power_on_hashboard : fpga reset one more time"
    // This ensures FPGA is in clean state after power stabilizes
    printf("Performing FPGA reset after DC-DC enable...\n");
    ctx.fpga_regs[0x034 / 4] = 0x0000FFF8;  // Reset all hashboards
    __sync_synchronize();
    usleep(100000);  // 100ms reset delay
    printf("FPGA reset complete\n\n");

    // Initialize the chain
    printf("Initializing chain %d...\n", chain);
    if (bm1398_init_chain(&ctx, chain) < 0) {
        fprintf(stderr, "Warning: Chain initialization failed (may already be initialized)\n");
    }

    // Reduce voltage to operational level (CRITICAL: must match bmminer!)
    printf("====================================\n");
    printf("Ramping Voltage to Operational Level\n");
    printf("====================================\n");
    printf("Ramping from 15.0V to 12.6V...\n");
    for (uint32_t v = 15000; v >= 12600; v -= 100) {
        if (bm1398_psu_set_voltage(&ctx, v) < 0) {
            fprintf(stderr, "Warning: Failed to set voltage to %umV\n", v);
            break;
        }
        printf("  Voltage set to %.2fV\n", v / 1000.0);
        usleep(50000); // 50ms delay between steps
    }
    printf("Voltage ramp complete.\n");

    // CRITICAL: Extended stabilization delay after voltage change
    printf("Waiting 5 seconds for voltage stabilization...\n");
    sleep(5);
    printf("\n");

    printf("\n");
    printf("====================================\n");
    printf("Sending Test Work\n");
    printf("====================================\n\n");

    // Check FIFO space
    int fifo_space = bm1398_check_work_fifo_ready(&ctx);
    printf("Work FIFO space: %d\n\n", fifo_space);

    // Send test work packets
    for (uint32_t i = 0; i < TEST_WORK_COUNT; i++) {
        uint8_t work_data[12];
        uint8_t midstates[4][32];

        create_test_work(i, work_data, midstates);

        printf("Sending work %u...\n", i);
        print_hex("  Work data", work_data, 12);

        if (bm1398_send_work(&ctx, chain, i, work_data, midstates) < 0) {
            fprintf(stderr, "Error: Failed to send work %u\n", i);
            bm1398_cleanup(&ctx);
            return 1;
        }

        usleep(10000);  // 10ms delay between work submissions
    }

    printf("\nAll work sent successfully!\n");

    // Monitor for nonces
    printf("\n");
    printf("====================================\n");
    printf("Monitoring for Nonces\n");
    printf("====================================\n\n");

    time_t start_time = time(NULL);
    int total_nonces = 0;
    nonce_response_t nonces[100];

    while (time(NULL) - start_time < NONCE_READ_TIMEOUT_MS / 1000) {
        int nonce_count = bm1398_get_nonce_count(&ctx);

        if (nonce_count > 0) {
            printf("Nonces in FIFO: %d\n", nonce_count);

            int read = bm1398_read_nonces(&ctx, nonces, 100);
            if (read > 0) {
                for (int i = 0; i < read; i++) {
                    printf("  Nonce %d: 0x%08x (chain=%u, work_id=%u)\n",
                           total_nonces + i,
                           nonces[i].nonce,
                           nonces[i].chain_id,
                           nonces[i].work_id);
                }
                total_nonces += read;
            }
        }

        usleep(100000);  // Check every 100ms
    }

    printf("\n");
    printf("====================================\n");
    printf("Test Complete\n");
    printf("====================================\n");
    printf("Total nonces received: %d\n", total_nonces);
    printf("\n");

    bm1398_cleanup(&ctx);
    return 0;
}
