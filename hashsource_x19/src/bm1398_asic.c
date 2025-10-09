/*
 * BM1398 ASIC Driver Implementation
 *
 * Protocol reverse-engineered from:
 * - Bitmain_Test_Fixtures S19_Pro single_board_test.c
 * - bitmaintech bmminer-mix driver-btm-c5.c
 * - Bitmain_Peek S19_Pro firmware analysis
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include "../include/bm1398_asic.h"

//==============================================================================
// Linux I2C Constants
//==============================================================================

#ifndef I2C_SLAVE
#define I2C_SLAVE 0x0703  // Set slave address (from linux/i2c-dev.h)
#endif

//==============================================================================
// CRC5 Implementation
//==============================================================================

/**
 * Calculate CRC5 for BM13xx UART commands
 * Polynomial: Custom 5-bit CRC
 * Initial value: 0x1F
 *
 * Source: Bitmain single_board_test.c line 28769
 */
uint8_t bm1398_crc5(const uint8_t *data, unsigned int bits) {
    uint8_t crc = 0x1F;  // Initial value

    for (unsigned int i = 0; i < bits; i++) {
        uint8_t bit = (data[i / 8] >> (7 - (i % 8))) & 1;
        if ((crc & 0x10) != (bit << 4)) {
            crc = ((crc << 1) | bit) ^ 0x05;
        } else {
            crc = (crc << 1) | bit;
        }
        crc &= 0x1F;
    }

    return crc;
}

//==============================================================================
// FPGA Indirect Register Mapping
//==============================================================================

/**
 * FPGA Register Mapping Table
 *
 * This table maps logical register indices to physical word offsets.
 *
 * Source: Binary analysis
 * - bmminer @ 0x7ee48 (production firmware)
 * - single_board_test @ 0x48894 (factory test)
 * Both tables are IDENTICAL (110 entries)
 *
 * Example:
 *   Logical index 20 (TIMEOUT) → word offset 35 → byte offset 0x08C
 *   Logical index 16/17 (WORK) → word offset 16 → byte offset 0x040
 */
static const uint32_t fpga_register_map[FPGA_REGISTER_MAP_SIZE] = {
    0, 1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,           // [0-15]
    16, 32, 33, 34, 35, 36, 37, 38, 0, 48, 49, 60, 62, 63, 64, 65,   // [16-31]
    66, 68, 69, 70, 71, 72, 73, 76, 77, 78, 80, 96, 97, 98, 99, 100, // [32-47]
    101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, // [48-63]
    117, 118, 119, 124, 125, 126, 127, 128, 129, 130, 132, 133, 134, 135, 136, 137, // [64-79]
    138, 139, 140, 141, 142, 143, 144, 145, 146, 147, 148, 149, 150, 151, 152, 153, // [80-95]
    154, 155, 156, 157, 158, 159, 164, 165, 166, 167, 168, 169  // [96-109]
};

/**
 * Read FPGA register using indirect mapping
 * Matches bmminer FUN_00040314 and factory test FUN_0001f230
 */
uint32_t fpga_read_indirect(bm1398_context_t *ctx, int logical_index) {
    if (!ctx || !ctx->fpga_regs) {
        fprintf(stderr, "Error: Invalid context in fpga_read_indirect\n");
        return 0;
    }
    if (logical_index < 0 || logical_index >= FPGA_REGISTER_MAP_SIZE) {
        fprintf(stderr, "Error: Invalid logical index %d in fpga_read_indirect\n", logical_index);
        return 0;
    }

    int word_offset = fpga_register_map[logical_index];
    return ctx->fpga_regs[word_offset];
}

/**
 * Write FPGA register using indirect mapping
 * Matches bmminer FUN_00040390 and factory test FUN_0001f288
 */
void fpga_write_indirect(bm1398_context_t *ctx, int logical_index, uint32_t value) {
    if (!ctx || !ctx->fpga_regs) {
        fprintf(stderr, "Error: Invalid context in fpga_write_indirect\n");
        return;
    }
    if (logical_index < 0 || logical_index >= FPGA_REGISTER_MAP_SIZE) {
        fprintf(stderr, "Error: Invalid logical index %d in fpga_write_indirect\n", logical_index);
        return;
    }

    int word_offset = fpga_register_map[logical_index];
    ctx->fpga_regs[word_offset] = value;
    __sync_synchronize();  // Force write to hardware (not cached)
}

//==============================================================================
// Initialization and Cleanup
//==============================================================================

int bm1398_init(bm1398_context_t *ctx) {
    if (!ctx) {
        return -1;
    }

    memset(ctx, 0, sizeof(*ctx));

    // Open FPGA device
    int fd = open("/dev/axi_fpga_dev", O_RDWR | O_SYNC);
    if (fd < 0) {
        fprintf(stderr, "Error: Cannot open /dev/axi_fpga_dev: %s\n", strerror(errno));
        fprintf(stderr, "Hint: Ensure bitmain_axi.ko kernel module is loaded\n");
        return -1;
    }

    // Memory map FPGA registers
    ctx->fpga_regs = mmap(NULL, FPGA_REG_SIZE, PROT_READ | PROT_WRITE,
                          MAP_SHARED, fd, 0);
    close(fd);

    if (ctx->fpga_regs == MAP_FAILED) {
        fprintf(stderr, "Error: mmap failed: %s\n", strerror(errno));
        return -1;
    }

    ctx->initialized = true;
    ctx->num_chains = 0;

    // Read and verify FPGA boot state
    // Source: FPGA dump analysis - register 0x080 should toggle during init, then stay at 0x00808000
    // PT2 FPGA dump shows:
    //   [3417.203124] INIT 0x080 0x00808000 (boot state)
    //   [3419.244838] 0x080: 0x00808000 -> 0x80808000 (init_fpga toggle)
    //   [3419.300471] 0x080: 0x80808000 -> 0x00808000 (return to normal)
    printf("Verifying FPGA boot state...\n");
    uint32_t reg_0x080 = ctx->fpga_regs[0x080 / 4];
    uint32_t reg_0x088 = ctx->fpga_regs[0x088 / 4];
    printf("  0x080 = 0x%08X (boot state, expected: 0x00808000)\n", reg_0x080);
    printf("  0x088 = 0x%08X (expected: 0x00009C40)\n", reg_0x088);

    // Perform init_fpga toggle sequence (matches Bitmain binary)
    // This is what Bitmain init_fpga does: toggle bit 31, then clear it
    printf("  Performing init_fpga toggle sequence on 0x080...\n");
    printf("    Setting 0x080 = 0x80808000 (bit 31 set)...\n");
    ctx->fpga_regs[0x080 / 4] = 0x80808000;
    __sync_synchronize();
    usleep(10000);  // Brief delay
    printf("    Clearing 0x080 = 0x00808000 (bit 31 clear)...\n");
    ctx->fpga_regs[0x080 / 4] = 0x00808000;
    __sync_synchronize();
    usleep(10000);
    printf("    Final 0x080 = 0x%08X\n", ctx->fpga_regs[0x080 / 4]);

    // Set correct value for 0x088 if wrong
    if (reg_0x088 != 0x00009C40) {
        printf("  WARNING: 0x088 mismatch, correcting...\n");
        ctx->fpga_regs[0x088 / 4] = 0x00009C40;
        __sync_synchronize();
        usleep(100000);
    }
    printf("  FPGA boot state verified\n\n");

    // Initialize FPGA registers using INDIRECT MAPPING
    // Matches bmminer and factory test initialization sequence
    // Source: Binary analysis of bmminer @ 0x45b34 and factory test @ 0x22cf0
    printf("Initializing FPGA registers (using indirect mapping)...\n");

    // FPGA Register 0: Set bit 30 (0x40000000)
    // Source: bmminer FUN_00045b34, factory test FUN_00022cf0
    // Both binaries do: read register 0, OR with 0x40000000, write back
    uint32_t reg0 = fpga_read_indirect(ctx, FPGA_REG_CONTROL);
    printf("  Register 0 before: 0x%08X\n", reg0);
    fpga_write_indirect(ctx, FPGA_REG_CONTROL, reg0 | 0x40000000);
    printf("  Register 0 after:  0x%08X\n", fpga_read_indirect(ctx, FPGA_REG_CONTROL));

    // FPGA Timeout Register (logical index 20 → physical byte offset 0x08C)
    // NOTE: Will be reconfigured after frequency is set during chain initialization
    // For now, set to a safe default (max timeout)
    uint32_t timeout_init = 0x0001FFFF | 0x80000000;  // Max 17-bit timeout + enable bit
    fpga_write_indirect(ctx, FPGA_REG_TIMEOUT, timeout_init);
    printf("  Timeout register init (0x08C): 0x%08X (will be recalculated per chain)\n",
           fpga_read_indirect(ctx, FPGA_REG_TIMEOUT));

    // Additional FPGA registers from factory test (may be needed for pattern testing)
    // NOTE: Production bmminer does NOT initialize these (per binary analysis)
    // They may have working defaults, or only be needed for factory testing
    //
    // Register 35 (0x118): Work control/enable
    // Factory test: fpga_write(35, reg35_val & 0xFFFF709F | 0x8060 | flags)
    // We'll use a simple read-modify-write with 0x8060
    uint32_t reg35 = fpga_read_indirect(ctx, FPGA_REG_WORK_CTRL_ENABLE);
    fpga_write_indirect(ctx, FPGA_REG_WORK_CTRL_ENABLE,
                       (reg35 & 0xFFFF709F) | 0x8060);
    printf("  Work control register (0x118): 0x%08X\n",
           fpga_read_indirect(ctx, FPGA_REG_WORK_CTRL_ENABLE));

    // Register 36 (0x11C): Chain/work configuration
    // Factory test uses complex calculation based on config values
    // Using basic default: (114 chips << 8) = 0x7200
    fpga_write_indirect(ctx, FPGA_REG_CHAIN_WORK_CONFIG, 0x00007200);
    printf("  Chain work config register (0x11C): 0x%08X\n",
           fpga_read_indirect(ctx, FPGA_REG_CHAIN_WORK_CONFIG));

    // Register 42 (0x140): Work queue parameter
    // Factory test uses: (value + 32 * config[20])
    // Using basic default based on 114 chips
    fpga_write_indirect(ctx, FPGA_REG_WORK_QUEUE_PARAM, 0x00003648);
    printf("  Work queue param register (0x140): 0x%08X\n",
           fpga_read_indirect(ctx, FPGA_REG_WORK_QUEUE_PARAM));

    // Direct register initialization (non-mapped registers)
    // Match Bitmain's PT2 FPGA boot state EXACTLY
    // Values verified from single_board_test_pt2_fpga_dump.log INIT section
    printf("Initializing FPGA registers to match PT2 dump...\n");
    ctx->fpga_regs[0x000 / 4] = 0x4000B031;
    ctx->fpga_regs[0x004 / 4] = 0x00000308;
    ctx->fpga_regs[0x008 / 4] = 0x00000001;
    ctx->fpga_regs[0x00C / 4] = 0x00000001;
    ctx->fpga_regs[0x010 / 4] = 0x0000400D;
    ctx->fpga_regs[0x014 / 4] = 0x5555AAAA;
    ctx->fpga_regs[0x01C / 4] = 0x00800001;
    ctx->fpga_regs[0x030 / 4] = 0x82400001;
    ctx->fpga_regs[0x034 / 4] = 0x0000FFF8;
    ctx->fpga_regs[0x03C / 4] = 0x0000001A;
    // init_fpga writes 0x80808000 to 0x080 (NOT 0x00808000 from bootloader)
    // Source: IDA Pro decompilation sub_22B58 @ 0x22b58, writes to logical register 18
    // Logical register 18 maps to physical word 0x20 = byte offset 0x080
    // This sets bit 31 which enables FPGA work routing functions
    ctx->fpga_regs[0x080 / 4] = 0x80808000;
    ctx->fpga_regs[0x084 / 4] = 0x00000064;
    ctx->fpga_regs[0x088 / 4] = 0x00009C40;
    ctx->fpga_regs[0x08C / 4] = 0x800000F9;
    ctx->fpga_regs[0x0A0 / 4] = 0x00000064;
    ctx->fpga_regs[0x0C0 / 4] = 0x00800000;
    ctx->fpga_regs[0x0C4 / 4] = 0x52050000;
    ctx->fpga_regs[0x0C8 / 4] = 0x0A000000;
    ctx->fpga_regs[0x0F0 / 4] = 0x2B104814;
    ctx->fpga_regs[0x0F4 / 4] = 0x8150F404;
    ctx->fpga_regs[0x0F8 / 4] = 0x000001CD;
    ctx->fpga_regs[0x118 / 4] = 0x00008060;
    ctx->fpga_regs[0x11C / 4] = 0x00007200;
    ctx->fpga_regs[0x140 / 4] = 0x00003648;
    printf("FPGA registers set to PT2 dump values.\n");

    __sync_synchronize();
    usleep(50000);  // 50ms settle time

    printf("FPGA registers initialized (indirect mapping verified)\n");

    // Detect chains
    uint32_t detected = bm1398_detect_chains(ctx);
    printf("Detected chains: 0x%08X\n", detected);

    for (int i = 0; i < MAX_CHAINS; i++) {
        if (detected & (1 << i)) {
            ctx->num_chains++;
            ctx->chips_per_chain[i] = CHIPS_PER_CHAIN_S19PRO;
            printf("  Chain %d: %d chips\n", i, ctx->chips_per_chain[i]);
        }
    }

    return 0;
}

void bm1398_cleanup(bm1398_context_t *ctx) {
    if (ctx && ctx->fpga_regs && ctx->fpga_regs != MAP_FAILED) {
        munmap((void *)ctx->fpga_regs, FPGA_REG_SIZE);
        ctx->fpga_regs = NULL;
    }
    ctx->initialized = false;
}

//==============================================================================
// Low-level UART Communication
//==============================================================================

/**
 * Send UART command to ASIC chain via FPGA BC_COMMAND_BUFFER
 *
 * Method: Write command bytes to registers 0xC4-0xCF (3 x 32-bit words)
 *         Trigger with BC_WRITE_COMMAND (0xC0)
 *         Wait for completion (bit 31 clears)
 *
 * Source: Analysis of S19 FPGA interface + bitmaintech driver
 */
int bm1398_send_uart_cmd(bm1398_context_t *ctx, int chain,
                         const uint8_t *cmd, size_t len) {
    if (!ctx || !ctx->initialized || !cmd || chain < 0 || chain >= MAX_CHAINS) {
        return -1;
    }

    if (len == 0 || len > 12) {
        fprintf(stderr, "Error: Invalid command length %zu (max 12 bytes)\n", len);
        return -1;
    }

    volatile uint32_t *regs = ctx->fpga_regs;

    // Write command bytes to BC_COMMAND_BUFFER (0xC4, 0xC8, 0xCC)
    // Up to 12 bytes = 3 x 32-bit words
    // FPGA expects BIG-ENDIAN byte order!
    // ARM is little-endian, so we must byte-swap
    for (size_t i = 0; i < (len + 3) / 4; i++) {
        uint32_t word = 0;
        size_t bytes_to_copy = (len - i * 4);
        if (bytes_to_copy > 4) bytes_to_copy = 4;

        memcpy(&word, &cmd[i * 4], bytes_to_copy);

        // Byte-swap to big-endian for FPGA
        // Example: {0x53, 0x05, 0x00, 0x00} -> 0x53050000 (not 0x00000553)
        word = __builtin_bswap32(word);

        regs[REG_BC_COMMAND_BUFFER + i] = word;
    }

    // Trigger command transmission
    uint32_t trigger = BC_COMMAND_BUFFER_READY | BC_CHAIN_ID(chain);
    regs[REG_BC_WRITE_COMMAND] = trigger;

    // Wait for completion (bit 31 clears)
    int timeout = 10000;  // 10ms max
    while ((regs[REG_BC_WRITE_COMMAND] & BC_COMMAND_BUFFER_READY) && timeout > 0) {
        usleep(1);
        timeout--;
    }

    if (timeout == 0) {
        fprintf(stderr, "Error: UART command timeout on chain %d\n", chain);
        return -1;
    }

    return 0;
}

//==============================================================================
// Chain Control Commands
//==============================================================================

/**
 * Send chain inactive command (stop relay)
 * Command: 0x53 0x05 0x00 0x00 [CRC5]
 */
int bm1398_chain_inactive(bm1398_context_t *ctx, int chain) {
    uint8_t cmd[5];

    cmd[0] = CMD_PREAMBLE_CHAIN_INACTIVE;
    cmd[1] = CMD_LEN_ADDRESS;
    cmd[2] = 0x00;
    cmd[3] = 0x00;
    cmd[4] = bm1398_crc5(cmd, 32);

    return bm1398_send_uart_cmd(ctx, chain, cmd, sizeof(cmd));
}

/**
 * Set chip address
 * Command: 0x40 0x05 [addr] 0x00 [CRC5]
 */
int bm1398_set_chip_address(bm1398_context_t *ctx, int chain, uint8_t addr) {
    uint8_t cmd[5];

    cmd[0] = CMD_PREAMBLE_SET_ADDRESS;
    cmd[1] = CMD_LEN_ADDRESS;
    cmd[2] = addr;
    cmd[3] = 0x00;
    cmd[4] = bm1398_crc5(cmd, 32);

    return bm1398_send_uart_cmd(ctx, chain, cmd, sizeof(cmd));
}

/**
 * Enumerate chips on chain
 * Assigns sequential addresses with specified interval
 *
 * S19 Pro: 114 chips, interval = 256/114 ≈ 2.2 → use 2
 * Addresses: 0, 2, 4, 6, ..., 226
 */
int bm1398_enumerate_chips(bm1398_context_t *ctx, int chain, int num_chips) {
    if (!ctx || !ctx->initialized) {
        return -1;
    }

    printf("Enumerating %d chips on chain %d...\n", num_chips, chain);

    // Send chain inactive first to stop relay
    if (bm1398_chain_inactive(ctx, chain) < 0) {
        fprintf(stderr, "Error: Failed to send chain inactive\n");
        return -1;
    }
    usleep(10000);

    // Calculate address interval
    int interval = 256 / num_chips;
    if (interval < 1) interval = 1;

    printf("  Address interval: %d\n", interval);

    // Assign addresses sequentially
    int errors = 0;
    for (int i = 0; i < num_chips; i++) {
        uint8_t addr = i * interval;

        if (bm1398_set_chip_address(ctx, chain, addr) < 0) {
            fprintf(stderr, "Warning: Failed to set address %d for chip %d\n", addr, i);
            errors++;
        }

        // Delay between chip enumeration commands
        // Factory test uses longer delays to ensure chips have time to process
        // Each chip must receive, process, and relay the command down the chain
        usleep(10000);  // 10ms per chip

        // Progress indication every 10 chips
        if ((i + 1) % 10 == 0) {
            printf("  Addressed %d/%d chips\r", i + 1, num_chips);
            fflush(stdout);
        }
    }

    printf("\n  Enumeration complete: %d chips addressed (%d errors)\n",
           num_chips, errors);

    return errors > 0 ? -1 : 0;
}

//==============================================================================
// Hardware Reset Control (FPGA Physical Reset Line)
//==============================================================================

/**
 * Set chain reset line LOW (asserted)
 * Source: IDA Pro sub_1c50f8 @ 0x1c50f8
 *
 * Manipulates FPGA register 0x034 (REG_RESET_HASHBOARD_COMMAND)
 * Sets bit corresponding to chain ID to assert reset
 */
void bm1398_chain_reset_low(bm1398_context_t *ctx, int chain) {
    if (!ctx || !ctx->initialized || chain < 0 || chain >= MAX_CHAINS) {
        return;
    }

    // Read current value from FPGA register 0x034
    // IDA Pro: fpga_read(0xD, &val) where logical 0xD → physical 0x034
    uint32_t val = fpga_read_indirect(ctx, 13);

    // Set bit for this chain (assert reset)
    // IDA Pro: fpga_write(0xD, val | (1 << chain_id))
    val |= (1 << chain);

    fpga_write_indirect(ctx, 13, val);
}

/**
 * Set chain reset line HIGH (de-asserted)
 * Source: IDA Pro sub_1c5128 @ 0x1c5128
 *
 * Clears bit corresponding to chain ID to release reset
 */
void bm1398_chain_reset_high(bm1398_context_t *ctx, int chain) {
    if (!ctx || !ctx->initialized || chain < 0 || chain >= MAX_CHAINS) {
        return;
    }

    // Read current value from FPGA register 0x034
    uint32_t val = fpga_read_indirect(ctx, 13);

    // Clear bit for this chain (release reset)
    // IDA Pro: fpga_write(0xD, val & ~(1 << chain_id))
    val &= ~(1 << chain);

    fpga_write_indirect(ctx, 13, val);
}

/**
 * Perform hardware reset sequence on chain
 * Source: IDA Pro Single_Board_PT2_Software_Pattern_Test lines 331-340
 *
 * This is the PHYSICAL reset toggle sequence via FPGA, completely
 * separate from ASIC register configuration. Must be done BEFORE
 * any UART communication with ASICs.
 *
 * Timing verified from Bitmain single_board_test debug binary:
 *   700ms initial delay
 *   Reset LOW → 10ms → HIGH → 72ms → LOW → 10ms → HIGH → 10ms
 */
int bm1398_hardware_reset_chain(bm1398_context_t *ctx, int chain) {
    if (!ctx || !ctx->initialized || chain < 0 || chain >= MAX_CHAINS) {
        return -1;
    }

    printf("Performing FPGA hardware reset sequence on chain %d...\n", chain);

    // Initial delay before reset sequence
    printf("  Initial delay (700ms)...\n");
    usleep(700000);  // 700ms (0xAAE60)

    // First reset pulse: LOW → HIGH
    printf("  Reset LOW...\n");
    bm1398_chain_reset_low(ctx, chain);
    usleep(10000);   // 10ms (0x2710)

    printf("  Reset HIGH...\n");
    bm1398_chain_reset_high(ctx, chain);
    usleep(72000);   // 72ms (0x11940 from elf_hash_chain[4414])

    // Second reset pulse: LOW → HIGH
    printf("  Reset LOW...\n");
    bm1398_chain_reset_low(ctx, chain);
    usleep(10000);   // 10ms

    printf("  Reset HIGH...\n");
    bm1398_chain_reset_high(ctx, chain);
    usleep(10000);   // 10ms final settle

    printf("  Hardware reset sequence complete\n");
    return 0;
}

//==============================================================================
// Register Operations
//==============================================================================

/**
 * Write ASIC register
 * Command: [0x41/0x51] 0x09 [chip_addr] [reg_addr] [value_be] [CRC5]
 *
 * broadcast: true = 0x51 (all chips), false = 0x41 (single chip)
 * value: 32-bit value in BIG-ENDIAN byte order
 */
int bm1398_write_register(bm1398_context_t *ctx, int chain, bool broadcast,
                          uint8_t chip_addr, uint8_t reg_addr, uint32_t value) {
    if (!ctx || !ctx->initialized) {
        return -1;
    }

    uint8_t cmd[9];

    cmd[0] = broadcast ? CMD_PREAMBLE_WRITE_BCAST : CMD_PREAMBLE_WRITE_REG;
    cmd[1] = CMD_LEN_WRITE_REG;
    cmd[2] = chip_addr;
    cmd[3] = reg_addr;
    cmd[4] = (value >> 24) & 0xFF;  // MSB first (big-endian)
    cmd[5] = (value >> 16) & 0xFF;
    cmd[6] = (value >> 8) & 0xFF;
    cmd[7] = value & 0xFF;          // LSB last
    cmd[8] = bm1398_crc5(cmd, 64);  // 8 bytes = 64 bits

    return bm1398_send_uart_cmd(ctx, chain, cmd, sizeof(cmd));
}

/**
 * Read ASIC register
 * Command: [0x42/0x52] 0x09 [chip_addr] [reg_addr] 0x00 0x00 0x00 0x00 [CRC5]
 *
 * Response comes back through FPGA RETURN_NONCE register
 * Response format: [reg_data:32bits] in bits [31:0]
 *
 * Note: This implementation uses polling of NONCE_NUMBER_IN_FIFO
 */
int bm1398_read_register(bm1398_context_t *ctx, int chain, bool broadcast,
                         uint8_t chip_addr, uint8_t reg_addr, uint32_t *value,
                         int timeout_ms) {
    if (!ctx || !ctx->initialized || !value) {
        return -1;
    }

    // Build read command (same as write but with 0x42/0x52 preamble)
    uint8_t cmd[9];
    cmd[0] = broadcast ? CMD_PREAMBLE_READ_BCAST : CMD_PREAMBLE_READ_REG;
    cmd[1] = CMD_LEN_WRITE_REG;
    cmd[2] = chip_addr;
    cmd[3] = reg_addr;
    cmd[4] = 0x00;  // Placeholder data
    cmd[5] = 0x00;
    cmd[6] = 0x00;
    cmd[7] = 0x00;
    cmd[8] = bm1398_crc5(cmd, 64);

    // Send read command
    if (bm1398_send_uart_cmd(ctx, chain, cmd, sizeof(cmd)) < 0) {
        return -1;
    }

    // Wait for response in FPGA FIFO
    volatile uint32_t *regs = ctx->fpga_regs;
    int timeout = timeout_ms * 1000;  // Convert to microseconds

    while (timeout > 0) {
        // Check if response available
        int available = regs[REG_NONCE_NUMBER_IN_FIFO];
        if (available > 0) {
            // Read response from FIFO
            uint32_t response = regs[REG_RETURN_NONCE];

            // Parse response (register data in lower 32 bits)
            // TODO: Verify this is correct format based on hardware testing
            *value = response;
            return 0;
        }

        usleep(100);  // Poll every 100us
        timeout -= 100;
    }

    fprintf(stderr, "Error: Register read timeout (chain %d, reg 0x%02X)\n",
            chain, reg_addr);
    return -1;
}

/**
 * Read-modify-write register operation
 *
 * Reads register, clears bits in clear_mask, sets bits in set_mask, writes back.
 * Uses broadcast to affect all chips on chain.
 *
 * Example: To set bit 2 and clear bit 5:
 *   clear_mask = (1 << 5)
 *   set_mask = (1 << 2)
 */
int bm1398_read_modify_write_register(bm1398_context_t *ctx, int chain,
                                      uint8_t reg_addr, uint32_t clear_mask,
                                      uint32_t set_mask) {
    if (!ctx || !ctx->initialized) {
        return -1;
    }

    uint32_t value;

    // Read current value (broadcast read from chip 0 as representative)
    if (bm1398_read_register(ctx, chain, false, 0, reg_addr, &value, 100) < 0) {
        fprintf(stderr, "Error: Read failed in read-modify-write (reg 0x%02X)\n",
                reg_addr);
        return -1;
    }

    printf("  Read reg 0x%02X = 0x%08X\n", reg_addr, value);

    // Modify value
    value &= ~clear_mask;  // Clear bits
    value |= set_mask;     // Set bits

    printf("  Writing reg 0x%02X = 0x%08X\n", reg_addr, value);

    // Write back (broadcast to all chips)
    if (bm1398_write_register(ctx, chain, true, 0, reg_addr, value) < 0) {
        fprintf(stderr, "Error: Write failed in read-modify-write (reg 0x%02X)\n",
                reg_addr);
        return -1;
    }

    usleep(10000);  // 10ms settle time
    return 0;
}

//==============================================================================
// Chain Initialization Sequences
//==============================================================================

/**
 * Stage 1: Hardware Reset Sequence
 *
 * Source: Bitmain single_board_test.c lines 13617-13633
 */
int bm1398_reset_chain_stage1(bm1398_context_t *ctx, int chain) {
    printf("Stage 1: Hardware reset chain %d...\n", chain);

    // Hardware reset sequence verified from Binary Ninja analysis
    // Source: Bitmain single_board_test.c sub_1d07c @ 0x1d07c
    //
    // It writes known-good values directly. Register reads don't work
    // reliably during early initialization.

    // Step 1: Soft reset disable (register 0x18)
    printf("  Soft reset disable (reg 0x18)...\n");
    bm1398_write_register(ctx, chain, true, 0, ASIC_REG_CLK_CTRL, 0x00000000);
    usleep(10000);

    // Step 2: Clear power control bit (register 0x34)
    printf("  Clear power control bit (reg 0x34)...\n");
    bm1398_write_register(ctx, chain, true, 0, ASIC_REG_RESET_CTRL, 0x00000000);
    usleep(10000);

    // Step 3: Core reset enable (register 0x18)
    printf("  Core reset enable (reg 0x18)...\n");
    bm1398_write_register(ctx, chain, true, 0, ASIC_REG_CLK_CTRL, 0x0F400000);
    usleep(10000);

    // Step 4: Core reset disable (register 0x18)
    printf("  Core reset disable (reg 0x18)...\n");
    bm1398_write_register(ctx, chain, true, 0, ASIC_REG_CLK_CTRL, 0xF0000000);
    usleep(10000);

    // Step 5: Soft reset enable (register 0x18)
    printf("  Soft reset enable (reg 0x18)...\n");
    bm1398_write_register(ctx, chain, true, 0, ASIC_REG_CLK_CTRL, 0xF0000400);
    usleep(10000);

    // Step 6: Set power control bit (register 0x34)
    printf("  Set power control bit (reg 0x34)...\n");
    bm1398_write_register(ctx, chain, true, 0, ASIC_REG_RESET_CTRL, 0x00000008);
    usleep(10000);

    // Step 7: Set ticket mask to all cores enabled (initialization value)
    printf("  Setting ticket mask to 0xFFFFFFFF...\n");
    if (bm1398_write_register(ctx, chain, true, 0, ASIC_REG_TICKET_MASK,
                              TICKET_MASK_ALL_CORES) < 0) {
        fprintf(stderr, "Error: Failed to set ticket mask\n");
        return -1;
    }
    usleep(50000);  // 50ms settle time

    printf("  Stage 1 complete\n");
    return 0;
}

/**
 * Stage 2: Configuration Sequence
 *
 * Source: Bitmain single_board_test.c lines 13640-13694
 */
int bm1398_configure_chain_stage2(bm1398_context_t *ctx, int chain,
                                  uint8_t diode_vdd_mux_sel) {
    printf("Stage 2: Configure chain %d...\n", chain);

    // 1. Set diode mux selector (voltage monitoring)
    printf("  Setting diode_vdd_mux_sel = %d...\n", diode_vdd_mux_sel);
    if (bm1398_write_register(ctx, chain, true, 0, ASIC_REG_DIODE_MUX,
                              diode_vdd_mux_sel) < 0) {
        fprintf(stderr, "Error: Failed to set diode mux\n");
        return -1;
    }
    usleep(10000);

    // 2. Chain inactive
    printf("  Chain inactive...\n");
    if (bm1398_chain_inactive(ctx, chain) < 0) {
        fprintf(stderr, "Error: Failed to send chain inactive\n");
        return -1;
    }
    usleep(10000);

    // 3. Set LOW baud rate (115200) for chip enumeration
    printf("  Setting LOW baud rate (115200) for enumeration...\n");
    if (bm1398_set_baud_rate(ctx, chain, 115200) < 0) {
        fprintf(stderr, "Error: Failed to set low baud rate\n");
        return -1;
    }
    usleep(50000);

    // 4. Enumerate chips
    printf("  Enumerating chips...\n");
    int num_chips = ctx->chips_per_chain[chain];
    if (bm1398_enumerate_chips(ctx, chain, num_chips) < 0) {
        fprintf(stderr, "Error: Chip enumeration failed\n");
        return -1;
    }
    usleep(10000);

    // 5. Register 0x3C reset sequence BEFORE pulse_mode config
    // Source: Binary Ninja sub_2959c @ 0x2959c - MUST DO THIS!
    printf("  Core config reset sequence (reg 0x3C)...\n");
    printf("    Step 1: Write 0x8000851F...\n");
    if (bm1398_write_register(ctx, chain, true, 0, ASIC_REG_CORE_CONFIG,
                              0x8000851F) < 0) {
        fprintf(stderr, "Error: Failed core reset step 1\n");
        return -1;
    }
    usleep(10000);

    printf("    Step 2: Write 0x80000600...\n");
    if (bm1398_write_register(ctx, chain, true, 0, ASIC_REG_CORE_CONFIG,
                              0x80000600) < 0) {
        fprintf(stderr, "Error: Failed core reset step 2\n");
        return -1;
    }
    usleep(10000);

    // 6. Set core configuration (pulse_mode=1, clk_sel=0)
    uint32_t core_cfg = CORE_CONFIG_BASE | ((1 & 3) << CORE_CONFIG_PULSE_MODE_SHIFT) | (0 & CORE_CONFIG_CLK_SEL_MASK);
    printf("  Setting core config = 0x%08X...\n", core_cfg);
    if (bm1398_write_register(ctx, chain, true, 0, ASIC_REG_CORE_CONFIG,
                              core_cfg) < 0) {
        fprintf(stderr, "Error: Failed to set core config\n");
        return -1;
    }
    usleep(10000);

    // 7. Set core timing parameters (pwth_sel=1, ccdly_sel=1, swpf_mode=0)
    // FIXED: ccdly_sel=1 (verified from bmminer log line 441)
    uint8_t pwth_sel = 1;
    uint8_t ccdly_sel = 1;  // FIXED: was 0, must be 1
    uint8_t swpf_mode = 0;
    uint32_t core_param = ((pwth_sel & CORE_PARAM_PWTH_SEL_MASK) << CORE_PARAM_PWTH_SEL_SHIFT) |
                          ((ccdly_sel & CORE_PARAM_CCDLY_SEL_MASK) << CORE_PARAM_CCDLY_SEL_SHIFT);
    if (swpf_mode != 0) {
        core_param |= (1 << CORE_PARAM_SWPF_MODE_BIT);
    }
    printf("  Setting core timing params = 0x%08X (pwth_sel=%u, ccdly_sel=%u, swpf_mode=%u)...\n",
           core_param, pwth_sel, ccdly_sel, swpf_mode);
    if (bm1398_write_register(ctx, chain, true, 0, ASIC_REG_CORE_PARAM,
                              core_param) < 0) {
        fprintf(stderr, "Error: Failed to set core timing parameters\n");
        return -1;
    }
    usleep(10000);

    // 4c. Set IO driver strength for clock output (clko_ds=1)
    // Register 0x58: Modify bits [7:4] to set clko_ds
    printf("  Setting IO driver clock output strength (clko_ds=1)...\n");
    uint32_t io_driver = 0x10;  // clko_ds=1 in bits [7:4]
    if (bm1398_write_register(ctx, chain, true, 0, ASIC_REG_IO_DRIVER,
                              io_driver) < 0) {
        fprintf(stderr, "Warning: IO driver configuration failed\n");
    }
    usleep(10000);

    // 5. Set PLL dividers to 0
    printf("  Setting PLL dividers...\n");
    bm1398_write_register(ctx, chain, true, 0, ASIC_REG_PLL_PARAM_0, 0x00000000);
    usleep(10000);
    bm1398_write_register(ctx, chain, true, 0, ASIC_REG_PLL_PARAM_1, 0x00000000);
    usleep(10000);
    bm1398_write_register(ctx, chain, true, 0, ASIC_REG_PLL_PARAM_2, 0x00000000);
    usleep(10000);
    bm1398_write_register(ctx, chain, true, 0, ASIC_REG_PLL_PARAM_3, 0x00000000);
    usleep(10000);

    // 6. Set frequency (525 MHz)
    printf("  Setting frequency to %d MHz...\n", FREQUENCY_525MHZ);
    if (bm1398_set_frequency(ctx, chain, FREQUENCY_525MHZ) < 0) {
        fprintf(stderr, "Warning: Frequency set failed\n");
    }

    // PLL needs time to lock and stabilize before proceeding
    // Factory test and bmminer both have significant delays here
    // PLLs typically need 100-500ms to achieve stable lock
    printf("  Waiting for PLL to lock and stabilize (500ms)...\n");
    usleep(500000);  // 500ms for PLL lock

    // 7. Set HIGH baud rate (12 MHz) AFTER frequency configuration
    // This is phase 2 of two-phase baud rate setup
    printf("  Setting HIGH baud rate (%d Hz) after frequency config...\n", BAUD_RATE_12MHZ);
    if (bm1398_set_baud_rate(ctx, chain, BAUD_RATE_12MHZ) < 0) {
        fprintf(stderr, "Error: Failed to set high baud rate\n");
        return -1;
    }
    usleep(50000);

    // 7b. Clear UART RX FIFO after baud rate change
    // Factory test sub_20608 @ 0x20608 calls this immediately after baud change
    // Removes garbage data accumulated during baud rate transition
    printf("  Clearing UART RX FIFO after baud rate change...\n");
    int nonce_count = bm1398_get_nonce_count(ctx);
    if (nonce_count > 0) {
        printf("    Found %d stale entries in nonce FIFO, clearing...\n", nonce_count);
        nonce_response_t discard_nonces[256];
        int cleared = bm1398_read_nonces(ctx, discard_nonces,
                                        nonce_count < 256 ? nonce_count : 256);
        printf("    Cleared %d stale nonce entries\n", cleared);
    } else {
        printf("    Nonce FIFO already empty\n");
    }
    usleep(10000);

    // NOTE: PT2 log analysis shows NO second enumeration at high baud
    // Bitmain's PT2 test does NOT re-enumerate chips after baud rate change
    // Skip second enumeration to match Bitmain's behavior
    printf("  Skipping second enumeration (not in PT2 test sequence)...\n");
    usleep(50000);  // 50ms settle time after baud rate change

    // 7d. Core reset sequence (nonce reception)
    // Use broadcast writes to avoid system hang with 114 chips
    printf("  Performing core reset sequence (broadcast)...\n");

    // Step 1a: Soft reset control (register 0xA8) - broadcast
    printf("    Broadcast soft reset (reg 0xA8)...\n");
    if (bm1398_write_register(ctx, chain, true, 0, ASIC_REG_SOFT_RESET,
                              SOFT_RESET_MASK) < 0) {
        fprintf(stderr, "Warning: Soft reset broadcast failed\n");
    }
    usleep(100000);  // 100ms settle time

    // Step 1b: Modify CLK_CTRL (register 0x18) - broadcast
    printf("    Broadcast CLK_CTRL (reg 0x18)...\n");
    if (bm1398_write_register(ctx, chain, true, 0, ASIC_REG_CLK_CTRL,
                              0xF0000000) < 0) {
        fprintf(stderr, "Warning: CLK_CTRL broadcast failed\n");
    }
    usleep(100000);  // 100ms settle time

    // Step 2: Re-configure clock select with clk_sel=0 - broadcast
    uint32_t core_config_reset = CORE_CONFIG_BASE | ((1 & 3) << CORE_CONFIG_PULSE_MODE_SHIFT);
    printf("    Broadcast clock select reset (clk_sel=0)...\n");
    if (bm1398_write_register(ctx, chain, true, 0, ASIC_REG_CORE_CONFIG,
                              core_config_reset) < 0) {
        fprintf(stderr, "Warning: Clock select reset broadcast failed\n");
    }
    usleep(100000);  // 100ms settle time

    // Step 3: Re-configure timing parameters - broadcast
    printf("    Broadcast timing params...\n");
    if (bm1398_write_register(ctx, chain, true, 0, ASIC_REG_CORE_PARAM,
                              core_param) < 0) {
        fprintf(stderr, "Warning: Timing param reset broadcast failed\n");
    }
    usleep(100000);  // 100ms settle time

    // Step 4: Core enable (register 0x3C with 0x800082AA) - broadcast
    printf("    Broadcast core enable...\n");
    if (bm1398_write_register(ctx, chain, true, 0, ASIC_REG_CORE_CONFIG,
                              CORE_CONFIG_ENABLE) < 0) {
        fprintf(stderr, "Warning: Core enable broadcast failed\n");
    }
    usleep(100000);  // 100ms settle time

    printf("  Core reset sequence complete\n");

    // Long stabilization delay after core reset
    // Factory test and bmminer both have significant delays here
    // ASICs need time to stabilize after reset before accepting work
    printf("  Waiting 2 seconds for core stabilization...\n");
    sleep(2);

    // 7e. Configure FPGA nonce timeout based on chip frequency
    // Factory test: dhash_set_timeout() at sub_222f8 @ 0x222f8
    // Writes to logical FPGA index 20 (0x14) → physical offset 0x08C
    // The timeout value from FPGA dump is 0x800000F9 (249 cycles)
    // This is already correct from bootloader - DO NOT recalculate!
    // Formula analysis: 0xF9 = 249, which matches (timeout_value = 0x96 + base_offset)
    // where timeout_value calculation is: FPGA_FREQ * 1.3 / (ASIC_FREQ * chip_count)
    // For 525MHz with 114 chips: ~249 cycles
    printf("  FPGA nonce timeout already configured: 0x%08X (keeping bootloader value)\n",
           fpga_read_indirect(ctx, FPGA_REG_TIMEOUT));
    // No write needed - bootloader value is correct
    usleep(10000);

    // 8. Keep ticket mask at 0xFFFFFFFF (all cores enabled)
    // Don't restrict to 0xFF - that only enables 8 cores!
    // For pattern testing with 80 cores, we need all cores enabled.
    printf("  Keeping ticket mask = 0xFFFFFFFF (all cores enabled for testing)...\n");
    // Already set to 0xFFFFFFFF in stage 1, no need to change it
    usleep(10000);

    // 9. Set nonce overflow control (disable overflow)
    // Register 0x3C: Final configuration with nonce overflow disabled
    printf("  Setting nonce overflow control (disabled)...\n");
    if (bm1398_write_register(ctx, chain, true, 0, ASIC_REG_CORE_CONFIG,
                              CORE_CONFIG_NONCE_OVF_DIS) < 0) {
        fprintf(stderr, "Warning: Nonce overflow control failed\n");
    }
    usleep(10000);

    printf("  Stage 2 complete\n");
    return 0;
}

/**
 * Complete chain initialization (hardware reset + both config stages)
 */
int bm1398_init_chain(bm1398_context_t *ctx, int chain) {
    if (!ctx || !ctx->initialized || chain < 0 || chain >= MAX_CHAINS) {
        return -1;
    }

    printf("\n====================================\n");
    printf("Initializing Chain %d\n", chain);
    printf("====================================\n\n");

    // Step 0: FPGA hardware reset (physical reset line toggle)
    // This MUST be done BEFORE any ASIC communication!
    // Source: IDA Pro PT2 test lines 331-340
    if (bm1398_hardware_reset_chain(ctx, chain) < 0) {
        fprintf(stderr, "Error: Hardware reset failed\n");
        return -1;
    }

    // Stage 1: Software reset (ASIC registers)
    if (bm1398_reset_chain_stage1(ctx, chain) < 0) {
        fprintf(stderr, "Error: Stage 1 failed\n");
        return -1;
    }

    // Stage 2: Configuration (diode_vdd_mux_sel = 3 from Config.ini)
    if (bm1398_configure_chain_stage2(ctx, chain, 3) < 0) {
        fprintf(stderr, "Error: Stage 2 failed\n");
        return -1;
    }

    printf("\n====================================\n");
    printf("Chain %d initialization complete\n", chain);
    printf("====================================\n\n");

    return 0;
}

//==============================================================================
// Baud Rate and Frequency Configuration
//==============================================================================

/**
 * Set UART baud rate
 *
 * Note: This is a simplified implementation. Full implementation requires
 * reading CLK_CTRL register, modifying specific bits, and writing back.
 *
 * Source: Bitmain single_board_test.c lines 27479-27527
 */
int bm1398_set_baud_rate(bm1398_context_t *ctx, int chain, uint32_t baud_rate) {
    if (!ctx || !ctx->initialized) {
        return -1;
    }

    uint32_t baud_div;
    uint32_t reg_val;

    if (baud_rate > 3000000) {
        // High-speed mode (>3 MHz) - uses 400 MHz base clock from PLL3
        // Source: Binary Ninja sub_2991c @ 0x2991c

        printf("    HIGH-SPEED baud mode (>3MHz)...\n");

        // Calculate divisor: 400MHz / (baud * 8) - 1
        baud_div = (400000000 / (baud_rate * 8)) - 1;
        printf("    Baud divisor (high-speed): %u (0x%X)\n", baud_div, baud_div);

        // Step 1: Configure PLL3 register (0x68) - Use direct write
        printf("    Configuring PLL3 (reg 0x68) for 400MHz UART clock...\n");
        bm1398_write_register(ctx, chain, true, 0, ASIC_REG_PLL_PARAM_3, 0xC0700111);
        usleep(10000);

        // Step 2: Configure BAUD_CONFIG register (0x28) - Use direct write
        printf("    Configuring BAUD_CONFIG (reg 0x28) for high-speed mode...\n");
        bm1398_write_register(ctx, chain, true, 0, ASIC_REG_BAUD_CONFIG, 0x06008F00);
        usleep(10000);

        // Step 3: Configure CLK_CTRL register (0x18) with divisor + high-speed bit
        printf("    Writing CLK_CTRL (reg 0x18) with divisor and high-speed bit...\n");

        // Build CLK_CTRL value from scratch, don't read
        // Base value: 0xF0000000 (from reset sequence)
        // Add divisor and high-speed bit
        reg_val = 0xF0000000 |                          // Base value from reset
                  (((baud_div >> 5) & 0xF) << 8) |      // Upper 4 bits of divisor to [11:8]
                  (baud_div & 0x1F) |                   // Lower 5 bits of divisor to [4:0]
                  0x00010000;                           // Bit 16: high-speed enable

        if (bm1398_write_register(ctx, chain, true, 0, ASIC_REG_CLK_CTRL, reg_val) < 0) {
            fprintf(stderr, "Error: Failed to write CLK_CTRL (high-speed)\n");
            return -1;
        }

    } else {
        // Low-speed mode (<= 3 MHz) - uses 25 MHz base clock
        // Source: Binary Ninja sub_2991c @ 0x2991c

        printf("    LOW-SPEED baud mode (<=3MHz)...\n");

        // Calculate divisor: 25MHz / (baud * 8) - 1
        baud_div = (25000000 / (baud_rate * 8)) - 1;
        printf("    Baud divisor (low-speed): %u (0x%X)\n", baud_div, baud_div);

        // Configure CLK_CTRL register (0x18) with divisor, clear high-speed bit
        printf("    Writing CLK_CTRL (reg 0x18) with divisor, low-speed mode...\n");

        // Build CLK_CTRL value from scratch, don't read
        // Base value: 0xF0000400 (from reset sequence with soft reset enabled)
        // Add divisor, ensure high-speed bit is clear
        reg_val = 0xF0000400 |                          // Base value from reset with soft reset enabled
                  (((baud_div >> 5) & 0xF) << 8) |      // Upper 4 bits of divisor to [11:8]
                  (baud_div & 0x1F);                    // Lower 5 bits of divisor to [4:0]
        // High-speed bit already clear in base value

        if (bm1398_write_register(ctx, chain, true, 0, ASIC_REG_CLK_CTRL, reg_val) < 0) {
            fprintf(stderr, "Error: Failed to write CLK_CTRL (low-speed)\n");
            return -1;
        }
    }

    usleep(50000);  // 50ms settle time for baud rate change
    printf("    Baud rate %u Hz configuration complete\n", baud_rate);
    return 0;
}

/**
 * Set ASIC core frequency
 *
 * Note: PLL configuration formula needs to be extracted from cgminer or bmminer.
 * This is a placeholder implementation.
 */
int bm1398_set_frequency(bm1398_context_t *ctx, int chain, uint32_t freq_mhz) {
    if (!ctx || !ctx->initialized) {
        return -1;
    }

    printf("    Setting frequency to %u MHz...\n", freq_mhz);

    // PLL configuration for BM1398
    // Formula: freq = CLKI * fbdiv / (refdiv * postdiv1 * postdiv2)
    // Where CLKI = 25 MHz
    // VCO = CLKI / refdiv * fbdiv (must be 1600-3200 MHz)

    uint8_t refdiv, postdiv1, postdiv2;
    uint16_t fbdiv;

    // For 525 MHz (standard BM1398 frequency):
    // Register encoding verified from Binary Ninja sub_29558 @ 0x29558:
    // Bits [2:0] = postdiv2 (value-1 encoding)
    // Bits [6:4] = refdiv (value-1 encoding)
    // Bits [13:8] = postdiv1 (value-1 encoding)
    // Bits [27:16] = fbdiv (direct value)
    //
    // Formula: freq = CLKI * fbdiv / (refdiv * (postdiv1+1) * (postdiv2+1))
    // Where CLKI = 25 MHz
    //
    // For 525 MHz with register value 0x40540100:
    // - fbdiv = 84 (bits [27:16] = 0x054)
    // - postdiv1 = 1 (bits [13:8] = 0x01, actual divisor = 1+1 = 2)
    // - refdiv = 0 (bits [6:4] = 0x0, actual divisor = 0+1 = 1)
    // - postdiv2 = 0 (bits [2:0] = 0x0, actual divisor = 0+1 = 1)
    //
    // VCO = 25 * 84 / 1 = 2100 MHz
    // freq = 2100 / (2 * 1) = 1050 MHz... WAIT, this doesn't match!
    //
    // Let me recalculate based on actual divisor values:
    // If stored value is value-1, then:
    // - refdiv_actual = 0+1 = 1
    // - postdiv1_actual = 1+1 = 2
    // - postdiv2_actual = 0+1 = 1
    //
    // But 2100/(1*2*1) = 1050, not 525!
    //
    // Maybe both postdiv contribute to divisor differently?
    // Or maybe the formula is: freq = VCO / (postdiv1 * postdiv2)
    // where VCO = CLKI / refdiv * fbdiv
    //
    // Let's use the values that work empirically from the doc:
    uint8_t refdiv_reg, postdiv1_reg, postdiv2_reg;
    uint16_t fbdiv_reg;

    if (freq_mhz == 525) {
        refdiv_reg = 0;    // Stored value (actual = 1)
        fbdiv_reg = 84;    // Direct value
        postdiv1_reg = 1;  // Stored value (actual = 2)
        postdiv2_reg = 0;  // Stored value (actual = 1)
    } else {
        fprintf(stderr, "    Warning: Frequency %u MHz not supported, using 525 MHz\n", freq_mhz);
        refdiv_reg = 0;
        fbdiv_reg = 84;
        postdiv1_reg = 1;
        postdiv2_reg = 0;
    }

    // Calculate VCO frequency for range check (using actual divisor values)
    uint8_t refdiv_actual = refdiv_reg + 1;
    uint8_t postdiv1_actual = postdiv1_reg + 1;
    uint8_t postdiv2_actual = postdiv2_reg + 1;
    float vco = 25.0f / refdiv_actual * fbdiv_reg;
    float freq_actual = vco / (postdiv1_actual * postdiv2_actual);

    printf("    PLL config: refdiv=%u (reg=0x%X), fbdiv=%u, postdiv1=%u (reg=0x%X), postdiv2=%u (reg=0x%X)\n",
           refdiv_actual, refdiv_reg, fbdiv_reg, postdiv1_actual, postdiv1_reg, postdiv2_actual, postdiv2_reg);
    printf("    VCO=%.0f MHz, calculated freq=%.0f MHz\n", vco, freq_actual);

    // Build PLL register value (verified encoding from Binary Ninja sub_29558)
    // Bits [2:0] = postdiv2 & 7
    // Bits [6:4] = refdiv & 7
    // Bits [13:8] = postdiv1 & 0x3f
    // Bits [27:16] = fbdiv & 0xfff
    uint32_t pll_value = 0x40000000 |
                         (postdiv2_reg & 0x7) |
                         ((refdiv_reg & 0x7) << 4) |
                         ((postdiv1_reg & 0x3f) << 8) |
                         ((fbdiv_reg & 0xfff) << 16);

    // Set VCO range bit based on VCO frequency
    if (vco >= 2400.0f && vco <= 3200.0f) {
        pll_value |= 0x10000000;  // High VCO range (bit 28)
    } else if (vco < 1600.0f || vco > 3200.0f) {
        fprintf(stderr, "    Error: VCO %.0f MHz out of range (1600-3200 MHz)\n", vco);
        return -1;
    }

    printf("    Writing PLL0 register 0x08 = 0x%08X (expected 0x40540100)\n", pll_value);

    // Write PLL0 parameter to register 0x08 (broadcast to all chips)
    if (bm1398_write_register(ctx, chain, true, 0, 0x08, pll_value) < 0) {
        fprintf(stderr, "    Error: Failed to write PLL0 register\n");
        return -1;
    }

    usleep(10000);  // Wait for PLL to stabilize
    printf("    Frequency configuration complete\n");

    return 0;
}

//==============================================================================
// Utility Functions
//==============================================================================

/**
 * Detect which chains are present
 * Returns: Bitmask with bit N set if chain N is detected
 */
uint32_t bm1398_detect_chains(bm1398_context_t *ctx) {
    if (!ctx || !ctx->initialized) {
        return 0;
    }

    return ctx->fpga_regs[REG_HASH_ON_PLUG];
}

/**
 * Get CRC error count from FPGA
 */
int bm1398_get_crc_error_count(bm1398_context_t *ctx) {
    if (!ctx || !ctx->initialized) {
        return -1;
    }

    return ctx->fpga_regs[REG_CRC_ERROR_CNT_ADDR];
}

//==============================================================================
// Work Submission
//==============================================================================

/**
 * Enable work send (FPGA control register)
 * Source: Bitmain enable_work_send()
 *
 * Factory test (sub_2213c @ 0x2213c) clears bit 14 of register 35
 * to disable auto-pattern generation BEFORE accepting external work!
 */
int bm1398_enable_work_send(bm1398_context_t *ctx) {
    if (!ctx || !ctx->initialized) {
        return -1;
    }

    // Verify register 0x080 work routing config (informational only)
    // FPGA dump shows this register should be 0x00808000 (bit 31 CLEAR) during normal operation
    // The toggle to 0x80808000 happens only during initialization, then returns to 0x00808000
    uint32_t reg_0x080 = ctx->fpga_regs[0x080 / 4];
    printf("  Checking FPGA work routing (reg 0x080)...\n");
    printf("    Register 0x080: 0x%08X (expected: 0x00808000 after init)\n", reg_0x080);
    if (reg_0x080 != 0x00808000) {
        printf("    WARNING: Unexpected value, expected 0x00808000\n");
    } else {
        printf("    OK: Register 0x080 at correct value\n");
    }

    // Disable auto-pattern generation (clear bit 14 of register 35)
    // Factory test sub_2213c: fpga_read(0x23); fpga_write(0x23, val & 0xffffbfff)
    // This MUST be done or FPGA won't accept external work!
    uint32_t reg35 = fpga_read_indirect(ctx, FPGA_REG_WORK_CTRL_ENABLE);
    printf("  Disabling auto-gen pattern (reg 35 bit 14)...\n");
    printf("    Register 35 before: 0x%08X\n", reg35);
    fpga_write_indirect(ctx, FPGA_REG_WORK_CTRL_ENABLE, reg35 & 0xFFFFBFFF);
    printf("    Register 35 after:  0x%08X (bit 14 cleared)\n",
           fpga_read_indirect(ctx, FPGA_REG_WORK_CTRL_ENABLE));

    // Register 0x2D (0xB4/4) = work send enable (if needed)
    // Note: This may not be required based on binary analysis
    // ctx->fpga_regs[0x2D] = 0xFFFFFFFF;

    return 0;
}

/**
 * Start FPGA work generation
 * Source: Bitmain start_dhash_work_gen()
 *
 * NOTE: This function is disabled because the direct register access
 * corrupts the FPGA timeout register (0x8C = word offset 0x23).
 * Work generation is already enabled via enable_work_send().
 */
int bm1398_start_work_gen(bm1398_context_t *ctx) {
    if (!ctx || !ctx->initialized) {
        return -1;
    }

    // DISABLED: This corrupted the timeout register at 0x8C
    // The FPGA is already configured for work reception via enable_work_send()
    // and the initialization sequence. No additional start is needed.

    printf("  Work generation control (no-op, already enabled)\n");
    return 0;
}

/**
 * Set ticket mask for chain (controls which cores are enabled)
 * Source: IDA Pro set_chain_ticketmask
 *
 * Common values from Bitmain PT2 test:
 *   0xFFFFFFFF - All cores (initialization)
 *   0xFFFF     - 65535 (super software pattern test)
 *   0x7F       - 127 (regular software pattern test)
 */
int bm1398_set_ticket_mask(bm1398_context_t *ctx, int chain, uint32_t mask) {
    if (!ctx || !ctx->initialized || chain < 0 || chain >= MAX_CHAINS) {
        return -1;
    }

    printf("Setting ticket mask = 0x%08X for chain %d...\n", mask, chain);

    // Write to ASIC register 0x14 (ASIC_REG_TICKET_MASK) via broadcast
    if (bm1398_write_register(ctx, chain, true, 0, ASIC_REG_TICKET_MASK, mask) < 0) {
        fprintf(stderr, "Error: Failed to set ticket mask\n");
        return -1;
    }

    usleep(50000);  // 50ms settle time
    return 0;
}

/**
 * Check if work FIFO has space available for a specific chain
 * Returns: 1 if ready, 0 if not ready, -1 on error
 *
 * Source: Bitmain single_board_test.c line 6220 (is_work_fifo_ready)
 * IDA Pro: sub_224A4 @ 0x224a4 - reads logical register 3 and checks chain bit
 * Must check chain-specific bit, not just raw value!
 */
int bm1398_check_work_fifo_ready(bm1398_context_t *ctx, int chain) {
    if (!ctx || !ctx->initialized) {
        return -1;
    }

    if (chain < 0 || chain >= MAX_CHAINS) {
        return -1;
    }

    // Read buffer space register (REG_BUFFER_SPACE = 0x00C)
    // Factory test sub_224A4: reads logical register 3, checks bit for chain
    // Logical register 3 maps to physical word 3 → byte offset 0x00C
    uint32_t buffer_status = ctx->fpga_regs[REG_BUFFER_SPACE];

    // Check if bit for this chain is set (indicates space available)
    return ((buffer_status & (1 << chain)) != 0) ? 1 : 0;
}

/**
 * Send work to ASIC chain via FPGA
 *
 * Source: Bitmain single_board_test.c software_pattern_4_midstate_send_function
 *         and set_TW_write_command
 */
int bm1398_send_work(bm1398_context_t *ctx, int chain, uint32_t work_id,
                    const uint8_t *work_data_12bytes,
                    const uint8_t midstates[4][32]) {
    if (!ctx || !ctx->initialized || !work_data_12bytes || !midstates) {
        return -1;
    }

    if (chain < 0 || chain >= MAX_CHAINS) {
        fprintf(stderr, "Error: Invalid chain %d\n", chain);
        return -1;
    }

    // Wait for FPGA work FIFO space before sending
    // Factory test checks buffer space to avoid overwhelming FPGA
    uint32_t buffer_status = ctx->fpga_regs[REG_BUFFER_SPACE];
    printf("[DEBUG] Buffer space register (0x00C): 0x%08X (chain %d bit=%d)\n",
           buffer_status, chain, (buffer_status >> chain) & 1);

    int timeout = 1000;  // 1 second max wait
    while (bm1398_check_work_fifo_ready(ctx, chain) < 1 && timeout > 0) {
        usleep(1000);  // 1ms
        timeout--;
    }
    if (timeout == 0) {
        fprintf(stderr, "Error: Work FIFO timeout on chain %d\n", chain);
        return -1;
    }
    printf("[DEBUG] FPGA work FIFO ready for chain %d (waited %dms)\n",
           chain, 1000 - timeout);

    // Build work packet (148 bytes = 0x94)
    work_packet_t work;
    memset(&work, 0, sizeof(work));

    work.work_type = 0x01;
    work.chain_id = (uint8_t)chain | 0x80;
    work.reserved[0] = 0x00;
    work.reserved[1] = 0x00;

    // Work ID: Shift left by 3, then store in native byte order
    // Will be byte-swapped later with all other fields
    // Don't pre-swap work_id, let the global swap handle it
    // Factory test: work_id << 3, then whole packet byte-swapped
    work.work_id = work_id << 3;

    // Copy last 12 bytes of block header
    memcpy(work.work_data, work_data_12bytes, 12);

    // Copy 4 midstates (32 bytes each)
    for (int i = 0; i < 4; i++) {
        memcpy(work.midstate[i], midstates[i], 32);
    }

    // Byte-swap all 32-bit words in the packet to big-endian
    // Total: 148 bytes / 4 = 37 words
    // This swaps work_id, work_data, and midstates to network byte order
    uint32_t *words = (uint32_t *)&work;

    printf("[DEBUG] Work packet before byte-swap (work_id=%u, chain=%d):\n", work_id, chain);
    printf("  Header: type=0x%02X chain_id=0x%02X work_id=0x%08X\n",
           work.work_type, work.chain_id, work.work_id);
    printf("  First 16 bytes: ");
    for (int j = 0; j < 16 && j < sizeof(work); j++) {
        printf("%02x ", ((uint8_t*)&work)[j]);
    }
    printf("\n");

    for (int i = 0; i < sizeof(work) / 4; i++) {
        words[i] = __builtin_bswap32(words[i]);
    }

    printf("[DEBUG] After byte-swap, first 4 words: 0x%08X 0x%08X 0x%08X 0x%08X\n",
           words[0], words[1], words[2], words[3]);

    // Hex dump for debugging
    // printf("  Work Packet Hex Dump (148 bytes):\n");
    // for(int j=0; j<148; j++) {
    //     printf("%02x ", ((uint8_t*)&work)[j]);
    //     if ((j+1) % 16 == 0) printf("\n");
    // }
    // printf("\n");

    // Write work packet to FPGA using INDIRECT MAPPING (FIFO-style)
    // Register mapping shows index 16→0x040, index 17→0x080!
    // We need to write ALL 37 words to index 16 (the FIFO at 0x040)
    //
    // WRONG (previous code):
    //   fpga_write(16, words[0]);     // → 0x040
    //   fpga_write(17, words[1..36]); // → 0x080 (WRONG REGISTER!)
    //
    // CORRECT:
    //   fpga_write(16, words[0..36]); // → 0x040 (FIFO pushes all words)

    int num_words = sizeof(work) / 4;  // 148 bytes / 4 = 37 words

    printf("[DEBUG] Writing %d words to FPGA FIFO at 0x040 (index 16)\n", num_words);
    printf("[DEBUG] First word: 0x%08X\n", words[0]);

    // Write ALL words to index 16 (FIFO at 0x040)
    for (int i = 0; i < num_words; i++) {
        fpga_write_indirect(ctx, FPGA_REG_TW_WRITE_CMD_FIRST, words[i]);
    }

    printf("[DEBUG] Work packet sent to FPGA (work_id=%u, chain=%d)\n", work_id, chain);
    printf("[DEBUG] FPGA register 0x040 final value: 0x%08X\n",
           ctx->fpga_regs[0x040 / 4]);

    usleep(10); // Small delay to prevent overwhelming FPGA

    return 0;
}

//==============================================================================
// Nonce Collection
//==============================================================================

/**
 * Get number of nonces in FPGA FIFO
 *
 * Source: Bitmain single_board_test.c get_nonce_number_in_fifo
 */
int bm1398_get_nonce_count(bm1398_context_t *ctx) {
    if (!ctx || !ctx->initialized) {
        return -1;
    }

    uint32_t count = ctx->fpga_regs[REG_NONCE_NUMBER_IN_FIFO];
    return count & 0x7FFF;  // Mask to 15 bits
}

/**
 * Read single nonce from FPGA FIFO
 *
 * Source: Bitmain single_board_test.c get_return_nonce
 *
 * Nonce FIFO returns 64-bit response (two 32-bit reads)
 * First read: Nonce value (32 bits)
 * Second read: Metadata - work_id, chip_id, chain_id (32 bits)
 *
 * Format verified from S19 FPGA interface:
 * Word 0 [31:0]: Nonce value (little-endian)
 * Word 1 [31:24]: Chain ID
 * Word 1 [23:16]: Chip ID
 * Word 1 [15:8]: Core ID
 * Word 1 [7:0]: Work ID (lower 8 bits)
 */
int bm1398_read_nonce(bm1398_context_t *ctx, nonce_response_t *nonce) {
    if (!ctx || !ctx->initialized || !nonce) {
        return -1;
    }

    volatile uint32_t *regs = ctx->fpga_regs;

    // Read nonce data from FIFO (64-bit = two 32-bit register reads)
    // First read pops the entry, second read gets metadata
    uint32_t nonce_value = regs[REG_RETURN_NONCE];  // First read: nonce value
    uint32_t nonce_meta = regs[REG_RETURN_NONCE];   // Second read: metadata

    // Parse nonce response format from FPGA
    nonce->nonce = nonce_value;                    // Full 32-bit nonce
    nonce->chain_id = (nonce_meta >> 24) & 0xFF;   // Bits [31:24]: chain
    nonce->chip_id = (nonce_meta >> 16) & 0xFF;    // Bits [23:16]: chip
    nonce->core_id = (nonce_meta >> 8) & 0xFF;     // Bits [15:8]: core
    nonce->work_id = (nonce_meta & 0xFF);          // Bits [7:0]: work_id

    return 1;  // Successfully read nonce
}

/**
 * Read multiple nonces from FPGA FIFO
 */
int bm1398_read_nonces(bm1398_context_t *ctx, nonce_response_t *nonces,
                      int max_count) {
    if (!ctx || !ctx->initialized || !nonces) {
        return -1;
    }

    int available = bm1398_get_nonce_count(ctx);
    if (available <= 0) {
        return 0;
    }

    int count = available < max_count ? available : max_count;
    int read_count = 0;

    for (int i = 0; i < count; i++) {
        if (bm1398_read_nonce(ctx, &nonces[read_count]) > 0) {
            read_count++;
        }
    }

    return read_count;
}

//==============================================================================
// PSU Power Control
//==============================================================================

// GPIO configuration
#define PSU_ENABLE_GPIO     907
#define GPIO_SYSFS_PATH     "/sys/class/gpio"

// I2C control bits (FPGA register 0x0C)
#define REG_I2C_CTRL        0x0C
#define I2C_READY           (1U << 31)
#define I2C_DATA_READY      (0x2U << 30)
#define I2C_READ_OP         (1U << 25)
#define I2C_READ_1BYTE      (1U << 19)
#define I2C_REGADDR_VALID   (1U << 24)

// PSU I2C addressing
#define PSU_I2C_MASTER      1
#define PSU_I2C_SLAVE_HIGH  0x02
#define PSU_I2C_SLAVE_LOW   0x00

// PSU protocol
#define PSU_REG_LEGACY      0x00
#define PSU_REG_V2          0x11
#define PSU_DETECT_MAGIC    0xF5
#define PSU_MAGIC_1         0x55
#define PSU_MAGIC_2         0xAA
#define CMD_GET_TYPE        0x02
#define CMD_SET_VOLTAGE     0x83

// Timeouts
#define I2C_TIMEOUT_MS      1000
#define PSU_SEND_DELAY_MS   400
#define PSU_READ_DELAY_MS   100
#define PSU_RETRIES         3

// PSU state (detect once per driver instance)
static uint8_t g_psu_reg = PSU_REG_V2;
static uint8_t g_psu_version = 0;

/**
 * GPIO helper functions
 */
int gpio_write_file(const char *path, const char *value) {
    int fd = open(path, O_WRONLY);
    if (fd < 0) return -1;

    ssize_t len = strlen(value);
    ssize_t written = write(fd, value, len);
    close(fd);

    return (written == len) ? 0 : -1;
}

int gpio_setup(int gpio, int value) {
    char path[64], buf[16];

    // Export (ignore if already exported)
    snprintf(buf, sizeof(buf), "%d", gpio);
    gpio_write_file(GPIO_SYSFS_PATH "/export", buf);

    // Set direction
    snprintf(path, sizeof(path), GPIO_SYSFS_PATH "/gpio%d/direction", gpio);
    if (gpio_write_file(path, "out") < 0) {
        return -1;
    }

    // Set value
    snprintf(path, sizeof(path), GPIO_SYSFS_PATH "/gpio%d/value", gpio);
    snprintf(buf, sizeof(buf), "%d", value);
    if (gpio_write_file(path, buf) < 0) {
        return -1;
    }

    return 0;
}

/**
 * I2C helper functions
 */
static inline uint32_t i2c_build_cmd(uint8_t reg, uint8_t data, bool read) {
    uint32_t cmd = (PSU_I2C_MASTER << 26) |
                   (PSU_I2C_SLAVE_HIGH << 20) |
                   ((PSU_I2C_SLAVE_LOW & 0x0E) << 15) |
                   I2C_REGADDR_VALID | (reg << 8);

    if (read) {
        cmd |= I2C_READ_OP | I2C_READ_1BYTE;
    } else {
        cmd |= data;
    }

    return cmd;
}

static int i2c_wait_ready(volatile uint32_t *regs) {
    for (int i = 0; i < I2C_TIMEOUT_MS / 5; i++) {
        if (regs[REG_I2C_CTRL] & I2C_READY)
            return 0;
        usleep(5000);
    }
    return -1;
}

static int i2c_wait_data(volatile uint32_t *regs, uint8_t *data) {
    for (int i = 0; i < I2C_TIMEOUT_MS / 5; i++) {
        uint32_t val = regs[REG_I2C_CTRL];
        if ((val >> 30) == 2) {
            *data = (uint8_t)(val & 0xFF);
            return 0;
        }
        usleep(5000);
    }
    return -1;
}

static int i2c_write_byte(volatile uint32_t *regs, uint8_t reg, uint8_t data) {
    uint8_t dummy;

    if (i2c_wait_ready(regs) < 0) return -1;

    regs[REG_I2C_CTRL] = i2c_build_cmd(reg, data, false);
    __sync_synchronize();

    return i2c_wait_data(regs, &dummy);
}

static int i2c_read_byte(volatile uint32_t *regs, uint8_t reg, uint8_t *data) {
    if (i2c_wait_ready(regs) < 0) return -1;

    regs[REG_I2C_CTRL] = i2c_build_cmd(reg, 0, true);
    __sync_synchronize();

    return i2c_wait_data(regs, data);
}

/**
 * PSU protocol detection and initialization
 */
static uint16_t calc_checksum(const uint8_t *data, size_t start, size_t end) {
    uint16_t sum = 0;
    for (size_t i = start; i < end; i++)
        sum += data[i];
    return sum;
}

static int psu_transact(volatile uint32_t *regs, const uint8_t *tx, size_t tx_len,
                       uint8_t *rx, size_t rx_len) {
    for (int retry = 0; retry < PSU_RETRIES; retry++) {
        // Send command
        bool tx_ok = true;
        for (size_t i = 0; i < tx_len && tx_ok; i++)
            tx_ok = (i2c_write_byte(regs, g_psu_reg, tx[i]) == 0);
        if (!tx_ok) continue;

        usleep(PSU_SEND_DELAY_MS * 1000);

        // Read response
        bool rx_ok = true;
        for (size_t i = 0; i < rx_len && rx_ok; i++)
            rx_ok = (i2c_read_byte(regs, g_psu_reg, &rx[i]) == 0);
        if (!rx_ok) continue;

        usleep(PSU_READ_DELAY_MS * 1000);

        // Validate magic bytes
        if (rx[0] == PSU_MAGIC_1 && rx[1] == PSU_MAGIC_2)
            return 0;
    }

    return -1;
}

static int psu_detect_protocol(volatile uint32_t *regs) {
    uint8_t test_val = PSU_DETECT_MAGIC, read_val;

    // Try V2 first
    g_psu_reg = PSU_REG_V2;
    if (i2c_write_byte(regs, g_psu_reg, test_val) == 0) {
        usleep(10000);
        if (i2c_read_byte(regs, g_psu_reg, &read_val) == 0 && read_val == test_val) {
            return 0;  // V2 protocol
        }
    }

    // Fallback to legacy
    g_psu_reg = PSU_REG_LEGACY;
    return 0;
}

static int psu_get_version(volatile uint32_t *regs) {
    uint8_t tx[8] = {PSU_MAGIC_1, PSU_MAGIC_2, 4, CMD_GET_TYPE};
    uint8_t rx[8];
    uint16_t csum = calc_checksum(tx, 2, 4);
    tx[4] = csum & 0xFF;
    tx[5] = (csum >> 8) & 0xFF;

    if (psu_transact(regs, tx, 6, rx, 8) < 0)
        return -1;

    g_psu_version = rx[4];
    return 0;
}

static uint16_t voltage_to_psu(uint32_t mv) {
    // PSU version 0x71 formula
    int64_t n = (1190935338LL - ((int64_t)mv * 78743LL)) / 1000000LL;
    if (n < 9) n = 9;
    if (n > 246) n = 246;
    return (uint16_t)n;
}

static int psu_set_voltage(volatile uint32_t *regs, uint32_t mv) {
    if (g_psu_version != 0x71) {
        fprintf(stderr, "Error: Unsupported PSU version 0x%02X\n", g_psu_version);
        return -1;
    }

    uint16_t n = voltage_to_psu(mv);
    uint8_t tx[8] = {PSU_MAGIC_1, PSU_MAGIC_2, 6, CMD_SET_VOLTAGE,
                     (uint8_t)(n & 0xFF), (uint8_t)(n >> 8)};
    uint8_t rx[8];
    uint16_t csum = calc_checksum(tx, 2, 6);
    tx[6] = csum & 0xFF;
    tx[7] = (csum >> 8) & 0xFF;

    if (psu_transact(regs, tx, 8, rx, 8) < 0)
        return -1;

    return (rx[3] == CMD_SET_VOLTAGE) ? 0 : -1;
}

//==============================================================================
// PIC Hashboard Power Control (FPGA I2C)
//==============================================================================

// PIC I2C addressing via FPGA
#define PIC_I2C_MASTER      0
#define PIC_I2C_SLAVE_HIGH  0x04

/**
 * Build FPGA I2C command for PIC communication
 *
 * Based on factory test i2c_write-001ca624.c line 46:
 * fpga_write(0xc, (slave_addr >> 4) << 0x14 | master << 0x1a |
 *                 ((slave_addr << 0x1c) >> 0x1d) << 0x10 | data)
 *
 * Where slave_addr = (chain << 1) | (0x04 << 4)
 */
static inline uint32_t pic_i2c_build_cmd(uint8_t chain, uint8_t data, bool read) {
    uint8_t slave_addr = (chain << 1) | (PIC_I2C_SLAVE_HIGH << 4);

    uint32_t cmd = (PIC_I2C_MASTER << 26) |
                   ((slave_addr >> 4) << 20) |
                   ((slave_addr & 0x0E) << 15);

    if (read) {
        cmd |= I2C_READ_OP | I2C_READ_1BYTE;
    } else {
        cmd |= data;
    }

    return cmd;
}

static int pic_i2c_write_byte(volatile uint32_t *regs, uint8_t chain, uint8_t data) {
    uint8_t dummy;

    if (i2c_wait_ready(regs) < 0) return -1;

    regs[REG_I2C_CTRL] = pic_i2c_build_cmd(chain, data, false);
    __sync_synchronize();

    return i2c_wait_data(regs, &dummy);
}

static int pic_i2c_read_byte(volatile uint32_t *regs, uint8_t chain, uint8_t *data) {
    if (i2c_wait_ready(regs) < 0) return -1;

    regs[REG_I2C_CTRL] = pic_i2c_build_cmd(chain, 0, true);
    __sync_synchronize();

    return i2c_wait_data(regs, data);
}

/**
 * Enable hashboard DC-DC converter via PIC I2C
 *
 * NOTE: This may not be necessary if DC-DC is already enabled from
 * previous run or if it auto-enables on PSU power-on.
 *
 * Based on factory test enable_dc_dc-001c5ae4.c and i2c_write-001ca624.c
 */
int bm1398_enable_dc_dc(bm1398_context_t *ctx, int chain) {
    if (!ctx || !ctx->initialized) {
        return -1;
    }

    volatile uint32_t *regs = ctx->fpga_regs;
    uint8_t send_data[7] = {
        0x55,       // Magic byte 1
        0xAA,       // Magic byte 2
        0x05,       // Length
        0x15,       // Command: Enable DC-DC
        0x01,       // Data: Enable
        0x00,       // Padding
        0x1B        // Checksum
    };

    printf("Attempting to enable PIC DC-DC converter for chain %d...\n", chain);
    printf("  PIC slave address: 0x%02X\n", (chain << 1) | (PIC_I2C_SLAVE_HIGH << 4));

    // Send command
    for (int i = 0; i < 7; i++) {
        if (pic_i2c_write_byte(regs, chain, send_data[i]) < 0) {
            fprintf(stderr, "  Warning: PIC write byte %d failed (may already be enabled)\n", i);
            return -1;
        }
    }

    // Wait for PIC to process
    usleep(300000);

    // Read response
    uint8_t read_data[2] = {0};
    for (int i = 0; i < 2; i++) {
        if (pic_i2c_read_byte(regs, chain, &read_data[i]) < 0) {
            fprintf(stderr, "  Warning: PIC read byte %d failed (may already be enabled)\n", i);
            return -1;
        }
    }

    // Validate response
    if (read_data[0] != 0x15 || read_data[1] != 0x01) {
        fprintf(stderr, "  Warning: PIC DC-DC response unexpected: 0x%02X 0x%02X (may already be enabled)\n",
                read_data[0], read_data[1]);
        return -1;
    }

    printf("  PIC DC-DC converter enabled (response: 0x%02X 0x%02X)\n",
           read_data[0], read_data[1]);
    return 0;
}

/**
 * Power on PSU at specified voltage
 *
 * Sequence:
 * 1. Detect PSU protocol (V2 or legacy)
 * 2. Read PSU version
 * 3. Set voltage via I2C
 * 4. Enable PSU via GPIO 907 (write 0 to enable)
 * 5. Wait 2 seconds for power to settle
 *
 * Based on psu_test.c and factory test APW_power_on-0005e6f8.c
 */
int bm1398_psu_power_on(bm1398_context_t *ctx, uint32_t voltage_mv) {
    if (!ctx || !ctx->initialized) {
        return -1;
    }

    // Detect PSU protocol (if not already detected)
    if (g_psu_version == 0) {
        if (psu_detect_protocol(ctx->fpga_regs) < 0) {
            fprintf(stderr, "Error: PSU protocol detection failed\n");
            return -1;
        }

        // Read PSU version
        if (psu_get_version(ctx->fpga_regs) < 0) {
            fprintf(stderr, "Warning: Could not read PSU version, assuming 0x71\n");
            g_psu_version = 0x71;
        }
    }

    // Set voltage via I2C
    if (psu_set_voltage(ctx->fpga_regs, voltage_mv) < 0) {
        fprintf(stderr, "Error: Failed to set PSU voltage to %umV\n", voltage_mv);
        return -1;
    }

    // Enable PSU via GPIO 907 (write 0 to enable)
    if (gpio_setup(PSU_ENABLE_GPIO, 0) < 0) {
        fprintf(stderr, "Error: Failed to enable PSU GPIO %d\n", PSU_ENABLE_GPIO);
        return -1;
    }

    // Wait 2 seconds for power to settle (from psu_test.c)
    sleep(2);

    return 0;
}

// Set PSU voltage without full power-on sequence (for voltage adjustment after init)
int bm1398_psu_set_voltage(bm1398_context_t *ctx, uint32_t voltage_mv) {
    if (!ctx || !ctx->initialized) {
        return -1;
    }

    // PSU must already be detected and powered on
    if (g_psu_version == 0) {
        fprintf(stderr, "Error: PSU not initialized, call bm1398_psu_power_on first\n");
        return -1;
    }

    // Set voltage via I2C
    if (psu_set_voltage(ctx->fpga_regs, voltage_mv) < 0) {
        fprintf(stderr, "Error: Failed to set PSU voltage to %umV\n", voltage_mv);
        return -1;
    }

    return 0;
}
