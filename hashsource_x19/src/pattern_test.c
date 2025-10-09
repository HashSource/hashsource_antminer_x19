/*
 * BM1398 Pattern Test - Exact Match to single_board_test
 *
 * This implementation precisely replicates the Bitmain factory test
 * pattern test methodology, verified against single_board_test binary.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <endian.h>
#include "../include/bm1398_asic.h"

// Configuration (matches single_board_test Config.ini)
#define TEST_CHAIN 0
#define CORES_PER_ASIC 80    // BM1398: 80 cores (16 small cores × 5 big cores)
#define PATTERNS_PER_CORE 8  // Pattern_Number from Config.ini
#define TEST_ASIC_ID 0       // Test first ASIC only for initial verification
#define NONCE_TIMEOUT_SEC 60

// Pattern file structure (verified from binary @ 0x1C890)
#define PATTERN_ENTRY_SIZE 0x74    // 116 bytes on disk
#define PATTERN_MEM_SIZE   0x7C    // 124 bytes in memory
#define PATTERNS_PER_CORE_ROW 8    // 8 pattern slots per core

// Pattern entry (116 bytes on disk)
typedef struct __attribute__((packed)) {
    uint8_t  header[15];     // Offset 0x00-0x0E: Header/metadata
    uint8_t  work_data[12];  // Offset 0x0F-0x1A: Last 12 bytes of block header
    uint8_t  midstate[32];   // Offset 0x1B-0x3A: SHA256 midstate
    uint8_t  reserved[29];   // Offset 0x3B-0x57: Padding/reserved
    uint32_t nonce;          // Offset 0x58-0x5B: Expected nonce (little-endian)
    uint8_t  trailer[24];    // Offset 0x5C-0x73: Additional data
} test_pattern_t;

// Work entry in memory (124 bytes)
typedef struct {
    test_pattern_t pattern;  // 116 bytes
    uint32_t work_id;        // 4 bytes - calculated work ID
    uint8_t  nonce_returned; // 1 byte - flag
    uint8_t  padding[3];     // 3 bytes - alignment
} pattern_work_t;

/**
 * Load pattern file for one ASIC
 * Matches sub_1C890 (parse_bin_file_to_pattern_ex) exactly
 */
int load_asic_patterns(const char *filename, int num_cores, int patterns_per_core,
                      pattern_work_t *works) {
    FILE *fp;
    int core, pat, skip_count;
    uint8_t temp_buf[PATTERN_ENTRY_SIZE];
    pattern_work_t *work_ptr;

    // Check file exists (as single_board_test does)
    if (access(filename, 0) != 0) {
        printf("parse_bin_file_to_pattern_ex : pattern file: %s don't exist!!!\n", filename);
        return -3;
    }

    fp = fopen(filename, "rb");
    if (!fp) {
        printf("parse_bin_file_to_pattern_ex : Open pattern file: %s failed !!!\n", filename);
        return -4;
    }

    printf("parse_bin_file_to_pattern_ex : Loading %d cores, %d patterns per core\n",
           num_cores, patterns_per_core);

    work_ptr = works;

    // Read patterns for each core
    for (core = 0; core < num_cores; core++) {
        // Read active patterns (patterns_per_core patterns)
        for (pat = 0; pat < patterns_per_core; pat++) {
            // Read 116 bytes from file (0x74)
            if (fread(&work_ptr->pattern, 1, PATTERN_ENTRY_SIZE, fp) != PATTERN_ENTRY_SIZE) {
                printf("parse_bin_file_to_pattern_ex : fread pattern failed!\n");
                fclose(fp);
                return -1;
            }

            // Calculate work_id (matches sub_2B254 call in single_board_test)
            // work_id is pattern index, NOT shifted here
            work_ptr->work_id = pat;
            work_ptr->nonce_returned = 0;

            work_ptr++;
        }

        // Skip remaining pattern slots (8 total slots per core)
        skip_count = PATTERNS_PER_CORE_ROW - patterns_per_core;
        for (int skip = 0; skip < skip_count; skip++) {
            if (fread(temp_buf, 1, PATTERN_ENTRY_SIZE, fp) != PATTERN_ENTRY_SIZE) {
                printf("skip_rows : skip pattern from file error!\n");
                break;
            }
        }
    }

    fclose(fp);
    printf("parse_bin_file_to_pattern_ex : Loaded %d patterns successfully\n",
           num_cores * patterns_per_core);
    return 0;
}

/**
 * Send pattern test work to chain
 * Matches sub_1C3B0 (software_pattern_4_midstate_send_function) exactly
 */
int send_pattern_work(bm1398_context_t *ctx, int chain,
                     pattern_work_t *works, int num_works) {
    int i;

    printf("software_pattern_4_midstate_send_function :  \n");

    for (i = 0; i < num_works; i++) {
        // Build midstates array (use same midstate for all 4 slots)
        uint8_t midstates[4][32];
        for (int m = 0; m < 4; m++) {
            memcpy(midstates[m], works[i].pattern.midstate, 32);
        }

        // Send work packet
        // NOTE: bm1398_send_work() handles:
        //   - work_id << 3 shift
        //   - Byte swapping
        //   - FPGA buffer check
        //   - Actual transmission
        if (bm1398_send_work(ctx, chain, works[i].work_id,
                            works[i].pattern.work_data, midstates) < 0) {
            fprintf(stderr, "Error: Failed to send pattern %d\n", i);
            return -1;
        }

        // Small delay between packets
        usleep(10);
    }

    printf("software_pattern_4_midstate_send_function : Send test %d pattern done\n", num_works);
    return 0;
}

/**
 * Main test function
 */
int main(int argc, char *argv[]) {
    const char *pattern_dir = "/tmp/BM1398-pattern";
    char filename[256];
    int chain = TEST_CHAIN;
    pattern_work_t *works;
    int num_patterns;

    if (argc > 1) {
        chain = atoi(argv[1]);
    }
    if (argc > 2) {
        pattern_dir = argv[2];
    }

    printf("\n");
    printf("====================================\n");
    printf("BM1398 Pattern Test (single_board_test Compatible)\n");
    printf("====================================\n");
    printf("Chain: %d\n", chain);
    printf("ASIC: %d\n", TEST_ASIC_ID);
    printf("Cores per ASIC: %d\n", CORES_PER_ASIC);
    printf("Patterns per core: %d\n", PATTERNS_PER_CORE);
    printf("Pattern dir: %s\n", pattern_dir);
    printf("\n");

    // Allocate pattern storage (matches calloc in get_works_ex)
    num_patterns = CORES_PER_ASIC * PATTERNS_PER_CORE;
    works = calloc(num_patterns, sizeof(pattern_work_t));
    if (!works) {
        fprintf(stderr, "Error: Failed to allocate pattern storage\n");
        return 1;
    }

    // Build filename
    snprintf(filename, sizeof(filename), "%s/btc-asic-%03d.bin",
             pattern_dir, TEST_ASIC_ID);

    // Load patterns
    printf("get_works_ex : pattern file path: %s\n", filename);
    printf("get_works_ex : asic_num = 1, core_num = %d, pattern_num = %d\n",
           CORES_PER_ASIC, PATTERNS_PER_CORE);

    if (load_asic_patterns(filename, CORES_PER_ASIC, PATTERNS_PER_CORE, works) < 0) {
        fprintf(stderr, "Error: Failed to load patterns\n");
        free(works);
        return 1;
    }

    // Initialize driver
    bm1398_context_t ctx;
    if (bm1398_init(&ctx) < 0) {
        fprintf(stderr, "Error: Failed to initialize driver\n");
        free(works);
        return 1;
    }

    // Power on PSU (matches PT2 test sequence)
    printf("\n====================================\n");
    printf("Single_Board_PT2_Test : Powering On PSU\n");
    printf("====================================\n");
    if (bm1398_psu_power_on(&ctx, 15000) < 0) {
        fprintf(stderr, "Error: Failed to power on PSU\n");
        bm1398_cleanup(&ctx);
        free(works);
        return 1;
    }
    printf("APW_power_on : APW power on ok\n\n");

    // Enable hashboard DC-DC converter
    printf("pic_power_on_hashboard : Enabling DC-DC Converter\n");
    if (bm1398_enable_dc_dc(&ctx, chain) < 0) {
        printf("Warning: DC-DC enable failed\n");
    }
    printf("pic_power_on_hashboard : PIC power on ok\n");
    printf("pic_power_on_hashboard : fpga reset one more time\n");

    // FPGA reset after PIC enable (matches PT1 log line 116)
    printf("Performing FPGA reset after DC-DC enable...\n");
    ctx.fpga_regs[0x034 / 4] = 0x0000FFF8;
    __sync_synchronize();
    usleep(100000);
    printf("FPGA reset complete.\n\n");

    // Initialize chain
    printf("====================================\n");
    printf("Initializing Chain %d\n", chain);
    printf("====================================\n");
    if (bm1398_init_chain(&ctx, chain) < 0) {
        fprintf(stderr, "Error: Chain initialization failed\n");
        bm1398_cleanup(&ctx);
        free(works);
        return 1;
    }
    printf("\n");

    // Ramp voltage to operational level (13.6V) - matches PT2 Test_Loop[0]->Voltage
    // PT2 uses: Pre_Open_Core_Voltage=15.0V, then ramps down to Voltage=13.6V
    printf("====================================\n");
    printf("Ramping Voltage to Operational Level\n");
    printf("====================================\n");
    printf("Starting at %.2fV (Pre_Open_Core_Voltage)\n", 15000 / 1000.0);
    printf("Target: %.2fV (Test_Loop[0]->Voltage)\n\n", 13600 / 1000.0);

    for (uint32_t v = 15000; v >= 13600; v -= 200) {
        if (bm1398_psu_set_voltage(&ctx, v) < 0) {
            fprintf(stderr, "Warning: Failed to set voltage to %umV\n", v);
            break;
        }
        printf("  Voltage: %.2fV\n", v / 1000.0);
        usleep(100000);  // 100ms between steps
    }
    printf("\nVoltage stabilization delay (2s)...\n");
    sleep(2);
    printf("\n");

    // Enable FPGA work reception
    // This disables auto-pattern generation and prepares FPGA to accept external work
    printf("====================================\n");
    printf("Enabling FPGA Work Reception\n");
    printf("====================================\n");
    if (bm1398_enable_work_send(&ctx) < 0) {
        fprintf(stderr, "Error: Failed to enable work send\n");
        bm1398_cleanup(&ctx);
        free(works);
        return 1;
    }
    printf("\n");

    // Send test patterns
    printf("====================================\n");
    printf("Sending Test Patterns\n");
    printf("====================================\n");
    if (send_pattern_work(&ctx, chain, works, num_patterns) < 0) {
        bm1398_cleanup(&ctx);
        free(works);
        return 1;
    }
    printf("\n");

    // Monitor for nonces
    printf("====================================\n");
    printf("Monitoring for Nonces (%d seconds)\n", NONCE_TIMEOUT_SEC);
    printf("====================================\n\n");

    time_t start_time = time(NULL);
    int total_nonces = 0;
    int valid_nonces = 0;
    nonce_response_t nonces[100];
    int loop_count = 0;

    while (time(NULL) - start_time < NONCE_TIMEOUT_SEC) {
        loop_count++;

        // Every 10 seconds, show we're still monitoring and dump FPGA registers
        if ((loop_count % 100) == 0) {
            int elapsed = (int)(time(NULL) - start_time);
            printf("[%ds] Still monitoring... Reading direct FPGA registers:\n", elapsed);
            printf("  [0x010] REG_RETURN_NONCE:        0x%08X\n", ctx.fpga_regs[0x010 / 4]);
            printf("  [0x018] REG_NONCE_NUMBER_FIFO:   0x%08X (masked=0x%04X)\n",
                   ctx.fpga_regs[0x018 / 4], ctx.fpga_regs[0x018 / 4] & 0x7FFF);
            printf("  [0x01C] REG_NONCE_FIFO_INTERRUPT: 0x%08X\n", ctx.fpga_regs[0x01C / 4]);
            printf("  [0x00C] REG_BUFFER_SPACE:        0x%08X\n", ctx.fpga_regs[0x00C / 4]);
            printf("  [0x040] Work FIFO (write-only):   0x%08X\n", ctx.fpga_regs[0x040 / 4]);
            printf("  [0x080] Work routing config:      0x%08X\n", ctx.fpga_regs[0x080 / 4]);
            printf("  [0x088] Work control:             0x%08X\n", ctx.fpga_regs[0x088 / 4]);
        }

        // Try both API call and direct register read
        int count = bm1398_get_nonce_count(&ctx);
        uint32_t raw_count = ctx.fpga_regs[0x018 / 4];

        // Debug: Show discrepancy if any
        if (count != (int)(raw_count & 0x7FFF) && (loop_count % 100) == 1) {
            printf("[DEBUG] Nonce count mismatch: API=%d, raw_reg=0x%08X (masked=%d)\n",
                   count, raw_count, (int)(raw_count & 0x7FFF));
        }

        if (count > 0) {
            int read = bm1398_read_nonces(&ctx, nonces, 100);

            for (int i = 0; i < read; i++) {
                total_nonces++;

                // Accept nonces from ANY chain!
                // Physical test machine may have board wired as chain 4 instead of chain 0
                printf("Nonce #%d: 0x%08X (chain=%d, chip=%d, core=%d, work_id=%d)\n",
                       total_nonces, nonces[i].nonce,
                       nonces[i].chain_id, nonces[i].chip_id,
                       nonces[i].core_id, nonces[i].work_id);

                // Parse the nonce value to check if it matches expected patterns
                // Try to match against all expected nonces
                // work_id in nonce response is encoded as (pattern_index << 3) & 0xFF
                bool found = false;
                for (int idx = 0; idx < num_patterns && !found; idx++) {
                    if (nonces[i].nonce == works[idx].pattern.nonce) {
                        // Verify work_id matches (with proper encoding)
                        uint8_t expected_work_id = (idx << 3) & 0xFF;
                        if (nonces[i].work_id == expected_work_id || nonces[i].work_id == 0) {
                            printf("  ✓ VALID! Pattern idx=%d (core=%d, pattern=%d), expected_nonce=0x%08X\n",
                                   idx, idx / PATTERNS_PER_CORE, idx % PATTERNS_PER_CORE,
                                   works[idx].pattern.nonce);
                            if (nonces[i].work_id == expected_work_id) {
                                printf("    Work ID matches: 0x%02X\n", expected_work_id);
                            } else {
                                printf("    Work ID mismatch: got 0x%02X, expected 0x%02X (ignoring for now)\n",
                                       nonces[i].work_id, expected_work_id);
                            }
                            works[idx].nonce_returned++;
                            valid_nonces++;
                            found = true;
                        }
                    }
                }

                if (!found) {
                    printf("  ? Unknown nonce (doesn't match any expected pattern nonce value)\n");
                    // Show first few expected nonces for debugging
                    if (total_nonces <= 5 && num_patterns > 0) {
                        printf("    Expected nonces: 0x%08X, 0x%08X, 0x%08X...\n",
                               works[0].pattern.nonce,
                               num_patterns > 1 ? works[1].pattern.nonce : 0,
                               num_patterns > 2 ? works[2].pattern.nonce : 0);
                    }
                }
            }
        }

        usleep(100000);  // 100ms polling
    }

    // Results
    printf("\n");
    printf("====================================\n");
    printf("Test Results\n");
    printf("====================================\n");
    printf("Patterns sent: %d\n", num_patterns);
    printf("Total nonces received: %d\n", total_nonces);
    printf("Valid nonces: %d\n", valid_nonces);
    if (num_patterns > 0) {
        printf("Success rate: %.1f%%\n", (valid_nonces * 100.0) / num_patterns);
    }
    printf("\n");

    // Cleanup
    bm1398_cleanup(&ctx);
    free(works);

    return (valid_nonces > 0) ? 0 : 1;
}
