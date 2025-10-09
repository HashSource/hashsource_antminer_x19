/**
 * BM1398 Pattern Test File Parser
 *
 * Parses and displays the structure of btc-asic-XXX.bin pattern files
 * used by Bitmain factory test fixtures.
 *
 * Usage: pattern_parser <pattern_file.bin> [options]
 *   -a, --all           Show all patterns (default: first 3 + last 8)
 *   -c, --core NUM      Show patterns for specific core
 *   -p, --pattern NUM   Show specific pattern number
 *   -s, --summary       Show summary only
 *   -v, --verbose       Show full hex dumps
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <getopt.h>

// Pattern file structure (verified from binary analysis)
#define BYTES_PER_CORE_ROW  7238
#define PATTERN_SIZE        116
#define PATTERNS_PER_CORE   62
#define ACTIVE_PATTERNS     8
#define NUM_CORES           80
#define HEADER_SIZE         6264  // First 54 patterns (54 × 116)
#define REMAINDER_SIZE      46
#define ACTIVE_START        6310  // Where last 8 patterns start
#define EXPECTED_FILE_SIZE  579072

// Pattern entry structure (116 bytes = 0x74)
typedef struct {
    uint8_t  header[15];      // Offset 0x00-0x0E: Header/metadata
    uint8_t  work_data[12];   // Offset 0x0F-0x1A: Last 12 bytes of block header
    uint8_t  midstate[32];    // Offset 0x1B-0x3A: SHA256 midstate
    uint8_t  reserved[29];    // Offset 0x3B-0x57: Reserved/padding
    uint32_t nonce;           // Offset 0x58-0x5B: Expected nonce (little-endian)
    uint8_t  trailer[24];     // Offset 0x5C-0x73: Trailer/additional data
} __attribute__((packed)) pattern_entry_t;

// Command-line options
typedef struct {
    int show_all;
    int show_summary;
    int verbose;
    int specific_core;
    int specific_pattern;
} options_t;

void print_hex(const uint8_t *data, size_t len, const char *prefix) {
    printf("%s", prefix);
    for (size_t i = 0; i < len; i++) {
        printf("%02x", data[i]);
    }
    printf("\n");
}

void print_pattern(const pattern_entry_t *pattern, int core, int pattern_num, int offset, int verbose) {
    printf("\n");
    printf("═══════════════════════════════════════════════════════════════════════════════\n");
    printf("Pattern %d, Core %d (Offset: 0x%04x / %d bytes)\n", pattern_num, core, offset, offset);
    printf("───────────────────────────────────────────────────────────────────────────────\n");

    printf("Expected Nonce:   0x%08x (%u)\n", pattern->nonce, pattern->nonce);
    print_hex(pattern->work_data, 12, "Work Data:        ");
    print_hex(pattern->midstate, 32, "Midstate:         ");

    if (verbose) {
        printf("\n");
        print_hex(pattern->header, 15, "Header (15b):     ");
        print_hex(pattern->reserved, 29, "Reserved (29b):   ");
        print_hex(pattern->trailer, 24, "Trailer (24b):    ");
    }
}

void print_file_summary(const char *filename, size_t file_size) {
    printf("\n");
    printf("═══════════════════════════════════════════════════════════════════════════════\n");
    printf("BM1398 PATTERN FILE STRUCTURE\n");
    printf("═══════════════════════════════════════════════════════════════════════════════\n");
    printf("\n");
    printf("File: %s\n", filename);
    printf("Size: %zu bytes (%.1f KB)\n", file_size, file_size / 1024.0);
    printf("\n");

    if (file_size == EXPECTED_FILE_SIZE) {
        printf("[OK] File size matches expected %d bytes\n", EXPECTED_FILE_SIZE);
    } else {
        printf("[WARNING] File size mismatch (expected %d bytes)\n", EXPECTED_FILE_SIZE);
    }

    printf("\n");
    printf("File Structure:\n");
    printf("───────────────────────────────────────────────────────────────────────────────\n");
    printf("  Cores:                    %d\n", NUM_CORES);
    printf("  Patterns per core:        %d\n", PATTERNS_PER_CORE);
    printf("  Total patterns:           %d\n", NUM_CORES * PATTERNS_PER_CORE);
    printf("\n");

    printf("Per-Core Layout (%d bytes):\n", BYTES_PER_CORE_ROW);
    printf("───────────────────────────────────────────────────────────────────────────────\n");
    printf("  1. First 54 patterns:     %d bytes (54 × %d)\n", HEADER_SIZE, PATTERN_SIZE);
    printf("  2. Padding:               %d bytes\n", REMAINDER_SIZE);
    printf("  3. Last 8 patterns:       %d bytes (8 × %d) <- ACTIVE patterns\n",
           ACTIVE_PATTERNS * PATTERN_SIZE, PATTERN_SIZE);
    printf("     Total:                 %d bytes\n", BYTES_PER_CORE_ROW);
    printf("\n");

    printf("Pattern Entry Format (%d bytes):\n", PATTERN_SIZE);
    printf("───────────────────────────────────────────────────────────────────────────────\n");
    printf("  Offset  Size  Field\n");
    printf("  0x00    15    Header/metadata\n");
    printf("  0x0F    12    Work data (last 12 bytes of block header)\n");
    printf("  0x1B    32    SHA256 midstate\n");
    printf("  0x3B    29    Reserved/padding\n");
    printf("  0x58    4     Expected nonce (little-endian)\n");
    printf("  0x5C    24    Trailer/additional data\n");
    printf("\n");

    printf("Usage by Factory Test:\n");
    printf("───────────────────────────────────────────────────────────────────────────────\n");
    printf("  - Only last 8 patterns per core are used\n");
    printf("  - First 54 patterns are ignored/reserved\n");
    printf("  - Total active patterns: %d cores × %d = %d patterns\n",
           NUM_CORES, ACTIVE_PATTERNS, NUM_CORES * ACTIVE_PATTERNS);
}

int parse_pattern_file(const char *filename, options_t *opts) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        fprintf(stderr, "Error: Cannot open file '%s'\n", filename);
        return -1;
    }

    // Get file size
    fseek(fp, 0, SEEK_END);
    size_t file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    // Print summary
    print_file_summary(filename, file_size);

    if (opts->show_summary) {
        fclose(fp);
        return 0;
    }

    printf("\n");
    printf("═══════════════════════════════════════════════════════════════════════════════\n");
    printf("PATTERN DATA\n");
    printf("═══════════════════════════════════════════════════════════════════════════════\n");

    // Read and parse patterns
    uint8_t *file_data = malloc(file_size);
    if (!file_data) {
        fprintf(stderr, "Error: Cannot allocate memory\n");
        fclose(fp);
        return -1;
    }

    if (fread(file_data, 1, file_size, fp) != file_size) {
        fprintf(stderr, "Error: Cannot read file\n");
        free(file_data);
        fclose(fp);
        return -1;
    }

    fclose(fp);

    // Parse patterns based on options
    for (int core = 0; core < NUM_CORES; core++) {
        // Skip if user wants specific core
        if (opts->specific_core >= 0 && core != opts->specific_core) {
            continue;
        }

        size_t core_offset = core * BYTES_PER_CORE_ROW;

        // Determine which patterns to show
        int start_pattern = 0;
        int end_pattern = PATTERNS_PER_CORE;

        if (!opts->show_all && opts->specific_pattern < 0 && opts->specific_core < 0) {
            // Default: show first 3 + last 8
            for (int i = 0; i < 3; i++) {
                size_t offset = core_offset + (i * PATTERN_SIZE);
                pattern_entry_t *pattern = (pattern_entry_t *)(file_data + offset);
                print_pattern(pattern, core, i, offset, opts->verbose);
            }

            printf("\n... (%d patterns omitted) ...\n", PATTERNS_PER_CORE - 3 - ACTIVE_PATTERNS);

            // Show last 8 (active patterns)
            for (int i = PATTERNS_PER_CORE - ACTIVE_PATTERNS; i < PATTERNS_PER_CORE; i++) {
                size_t offset = core_offset + ACTIVE_START +
                               ((i - (PATTERNS_PER_CORE - ACTIVE_PATTERNS)) * PATTERN_SIZE);
                pattern_entry_t *pattern = (pattern_entry_t *)(file_data + offset);

                printf("\n");
                printf("Pattern %d, Core %d (Offset: 0x%04zx / %zu bytes) [ACTIVE]\n",
                       i, core, offset, offset);
                printf("───────────────────────────────────────────────────────────────────────────────\n");
                printf("Expected Nonce:   0x%08x (%u)\n", pattern->nonce, pattern->nonce);
                print_hex(pattern->work_data, 12, "Work Data:        ");
                print_hex(pattern->midstate, 32, "Midstate:         ");

                if (opts->verbose) {
                    printf("\n");
                    print_hex(pattern->header, 15, "Header (15b):     ");
                    print_hex(pattern->reserved, 29, "Reserved (29b):   ");
                    print_hex(pattern->trailer, 24, "Trailer (24b):    ");
                }
            }

            // Only show first core by default
            if (opts->specific_core < 0) {
                printf("\n\n... (%d cores omitted) ...\n", NUM_CORES - 1);
                printf("\nUse --all to see all patterns, or -c NUM to see specific core\n");
                break;
            }

        } else if (opts->specific_pattern >= 0) {
            // Show specific pattern
            if (opts->specific_pattern >= PATTERNS_PER_CORE) {
                fprintf(stderr, "Error: Pattern %d out of range (0-%d)\n",
                        opts->specific_pattern, PATTERNS_PER_CORE - 1);
                continue;
            }

            size_t offset;
            if (opts->specific_pattern < 54) {
                offset = core_offset + (opts->specific_pattern * PATTERN_SIZE);
            } else {
                offset = core_offset + ACTIVE_START +
                        ((opts->specific_pattern - 54) * PATTERN_SIZE);
            }

            pattern_entry_t *pattern = (pattern_entry_t *)(file_data + offset);
            print_pattern(pattern, core, opts->specific_pattern, offset, opts->verbose);

        } else {
            // Show all patterns for this core
            for (int i = 0; i < PATTERNS_PER_CORE; i++) {
                size_t offset;
                if (i < 54) {
                    offset = core_offset + (i * PATTERN_SIZE);
                } else {
                    offset = core_offset + ACTIVE_START + ((i - 54) * PATTERN_SIZE);
                }

                pattern_entry_t *pattern = (pattern_entry_t *)(file_data + offset);

                if (i >= PATTERNS_PER_CORE - ACTIVE_PATTERNS) {
                    printf("\n");
                    printf("Pattern %d, Core %d (Offset: 0x%04zx / %zu bytes) [ACTIVE]\n",
                           i, core, offset, offset);
                    printf("───────────────────────────────────────────────────────────────────────────────\n");
                    printf("Expected Nonce:   0x%08x (%u)\n", pattern->nonce, pattern->nonce);
                    print_hex(pattern->work_data, 12, "Work Data:        ");
                    print_hex(pattern->midstate, 32, "Midstate:         ");

                    if (opts->verbose) {
                        printf("\n");
                        print_hex(pattern->header, 15, "Header (15b):     ");
                        print_hex(pattern->reserved, 29, "Reserved (29b):   ");
                        print_hex(pattern->trailer, 24, "Trailer (24b):    ");
                    }
                } else {
                    print_pattern(pattern, core, i, offset, opts->verbose);
                }
            }
        }
    }

    free(file_data);

    printf("\n");
    printf("═══════════════════════════════════════════════════════════════════════════════\n");
    printf("\n");

    return 0;
}

void print_usage(const char *prog_name) {
    printf("Usage: %s <pattern_file.bin> [options]\n", prog_name);
    printf("\n");
    printf("Options:\n");
    printf("  -a, --all              Show all patterns (default: first 3 + last 8 from core 0)\n");
    printf("  -c, --core NUM         Show patterns for specific core (0-79)\n");
    printf("  -p, --pattern NUM      Show specific pattern number (0-61)\n");
    printf("  -s, --summary          Show summary only (no pattern data)\n");
    printf("  -v, --verbose          Show full hex dumps of all fields\n");
    printf("  -h, --help             Show this help message\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s btc-asic-000.bin\n", prog_name);
    printf("  %s btc-asic-000.bin -s\n", prog_name);
    printf("  %s btc-asic-000.bin -c 0 -v\n", prog_name);
    printf("  %s btc-asic-000.bin -p 54\n", prog_name);
    printf("\n");
}

int main(int argc, char **argv) {
    options_t opts = {
        .show_all = 0,
        .show_summary = 0,
        .verbose = 0,
        .specific_core = -1,
        .specific_pattern = -1
    };

    static struct option long_options[] = {
        {"all",     no_argument,       0, 'a'},
        {"core",    required_argument, 0, 'c'},
        {"pattern", required_argument, 0, 'p'},
        {"summary", no_argument,       0, 's'},
        {"verbose", no_argument,       0, 'v'},
        {"help",    no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "ac:p:svh", long_options, NULL)) != -1) {
        switch (opt) {
            case 'a':
                opts.show_all = 1;
                break;
            case 'c':
                opts.specific_core = atoi(optarg);
                if (opts.specific_core < 0 || opts.specific_core >= NUM_CORES) {
                    fprintf(stderr, "Error: Core must be 0-%d\n", NUM_CORES - 1);
                    return 1;
                }
                break;
            case 'p':
                opts.specific_pattern = atoi(optarg);
                break;
            case 's':
                opts.show_summary = 1;
                break;
            case 'v':
                opts.verbose = 1;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "Error: Missing pattern file argument\n\n");
        print_usage(argv[0]);
        return 1;
    }

    return parse_pattern_file(argv[optind], &opts);
}
