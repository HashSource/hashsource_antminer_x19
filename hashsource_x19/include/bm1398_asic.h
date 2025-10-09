/*
 * BM1398 ASIC Driver for Antminer S19 Pro
 *
 * Hardware: 114 chips per chain, 3 chains total
 * UART: 12 MHz baudrate via FPGA
 * Frequency: 525 MHz target
 */

#ifndef BM1398_ASIC_H
#define BM1398_ASIC_H

#include <stdint.h>
#include <stdbool.h>

//==============================================================================
// FPGA Register Definitions
//==============================================================================

#define FPGA_REG_BASE               0x40000000
#define FPGA_REG_SIZE               5120

// FPGA Indirect Register Mapping

// Logical register indices (use with fpga_read_indirect/fpga_write_indirect)
#define FPGA_REG_CONTROL            0   // Maps to word 0 (0x000)
#define FPGA_REG_TW_WRITE_CMD_FIRST 16  // Maps to word 16 (0x040) - first word of work
#define FPGA_REG_TW_WRITE_CMD_REST  17  // Maps to word 16 (0x040) - rest of work (SAME!)
#define FPGA_REG_SPECIAL_18         18  // Maps to word 33 (0x084) - init register
#define FPGA_REG_TIMEOUT            20  // Maps to word 35 (0x08C)
#define FPGA_REG_WORK_CTRL_ENABLE   35  // Maps to word 70 (0x118) - Work control/auto-gen
#define FPGA_REG_CHAIN_WORK_CONFIG  36  // Maps to word 71 (0x11C)
#define FPGA_REG_WORK_QUEUE_PARAM   42  // Maps to word 80 (0x140)

// Direct FPGA register offsets (for non-mapped registers, word-aligned)
// These registers use DIRECT access, not the indirect mapping table
#define REG_HARDWARE_VERSION        (0x000 / 4)
#define REG_FAN_SPEED               (0x004 / 4)
#define REG_HASH_ON_PLUG            (0x008 / 4)
#define REG_BUFFER_SPACE            (0x00C / 4)
#define REG_RETURN_NONCE            (0x010 / 4)
#define REG_NONCE_NUMBER_IN_FIFO    (0x018 / 4)
#define REG_NONCE_FIFO_INTERRUPT    (0x01C / 4)
#define REG_IIC_COMMAND             (0x030 / 4)
#define REG_RESET_HASHBOARD_COMMAND (0x034 / 4)
#define REG_BC_WRITE_COMMAND        (0x0C0 / 4)
#define REG_BC_COMMAND_BUFFER       (0x0C4 / 4)
#define REG_FPGA_CHIP_ID_ADDR       (0x0F0 / 4)
#define REG_CRC_ERROR_CNT_ADDR      (0x0F8 / 4)

// FPGA register mapping table size
#define FPGA_REGISTER_MAP_SIZE      110

// BC_WRITE_COMMAND register bits
#define BC_COMMAND_BUFFER_READY     (1U << 31)
#define BC_COMMAND_EN_CHAIN_ID      (1U << 23)
#define BC_COMMAND_EN_NULL_WORK     (1U << 22)
#define BC_CHAIN_ID(id)             (((id) & 0xF) << 16)

// RETURN_NONCE register bits
#define NONCE_WORK_ID_OR_CRC        (1U << 31)
#define NONCE_INDICATOR             (1U << 7)
#define NONCE_CHAIN_NUMBER(v)       ((v) & 0xF)

//==============================================================================
// ASIC Register Definitions
//==============================================================================

// BM1398 ASIC registers
#define ASIC_REG_CHIP_ADDR          0x00
#define ASIC_REG_PLL_PARAM_0        0x08
#define ASIC_REG_HASH_COUNTING      0x10
#define ASIC_REG_TICKET_MASK        0x14
#define ASIC_REG_CLK_CTRL           0x18
#define ASIC_REG_WORK_ROLLING       0x1C
#define ASIC_REG_WORK_CONFIG        0x20
#define ASIC_REG_BAUD_CONFIG        0x28
#define ASIC_REG_RESET_CTRL         0x34
#define ASIC_REG_CORE_CONFIG        0x3C
#define ASIC_REG_CORE_PARAM         0x44
#define ASIC_REG_DIODE_MUX          0x54
#define ASIC_REG_IO_DRIVER          0x58
#define ASIC_REG_PLL_PARAM_1        0x60
#define ASIC_REG_PLL_PARAM_2        0x64
#define ASIC_REG_PLL_PARAM_3        0x68
#define ASIC_REG_VERSION_ROLLING    0xA4
#define ASIC_REG_SOFT_RESET         0xA8

// Core configuration values
#define CORE_CONFIG_BASE            0x80008700
#define CORE_CONFIG_PULSE_MODE_SHIFT 4
#define CORE_CONFIG_CLK_SEL_MASK    0x7
#define CORE_CONFIG_ENABLE          0x800082AA   // Core enable after reset
#define CORE_CONFIG_NONCE_OVF_DIS   0x80008D15   // Nonce overflow disabled

// Soft reset control values
#define SOFT_RESET_MASK             0x1F0        // Soft reset bits

// Core timing parameters (register 0x44)
#define CORE_PARAM_SWPF_MODE_BIT    0       // Bit 0: swpf_mode
#define CORE_PARAM_PWTH_SEL_SHIFT   3       // Bits 3-5: pwth_sel
#define CORE_PARAM_CCDLY_SEL_SHIFT  6       // Bits 6+: ccdly_sel
#define CORE_PARAM_PWTH_SEL_MASK    0x7
#define CORE_PARAM_CCDLY_SEL_MASK   0x3

// Ticket mask values
#define TICKET_MASK_ALL_CORES       0xFFFFFFFF
#define TICKET_MASK_256_CORES       0x000000FF

//==============================================================================
// UART Command Definitions
//==============================================================================

// Command preambles
#define CMD_PREAMBLE_SET_ADDRESS    0x40
#define CMD_PREAMBLE_WRITE_REG      0x41
#define CMD_PREAMBLE_READ_REG       0x42
#define CMD_PREAMBLE_WRITE_BCAST    0x51
#define CMD_PREAMBLE_READ_BCAST     0x52
#define CMD_PREAMBLE_CHAIN_INACTIVE 0x53

// Command lengths
#define CMD_LEN_ADDRESS             5
#define CMD_LEN_WRITE_REG           9

//==============================================================================
// Configuration Constants
//==============================================================================

#define MAX_CHAINS                  3
#define CHIPS_PER_CHAIN_S19PRO      114
#define CHIP_ADDRESS_INTERVAL       2

#define BAUD_RATE_12MHZ             12000000
#define FREQUENCY_525MHZ            525

//==============================================================================
// Data Structures
//==============================================================================

typedef struct {
    volatile uint32_t *fpga_regs;
    int num_chains;
    int chips_per_chain[MAX_CHAINS];
    bool initialized;
} bm1398_context_t;

typedef struct {
    uint32_t nonce;
    uint8_t chain_id;
    uint8_t chip_id;
    uint8_t core_id;
    uint16_t work_id;
} nonce_response_t;

// Work packet format (148 bytes = 0x94)
typedef struct __attribute__((packed)) {
    uint8_t work_type;          // 0x01
    uint8_t chain_id;           // chain | 0x80
    uint8_t reserved[2];        // 0x00, 0x00
    uint32_t work_id;           // Big-endian work ID
    uint8_t work_data[12];      // Last 12 bytes of block header
    uint8_t midstate[4][32];    // 4x 32-byte SHA256 midstates
} work_packet_t;

//==============================================================================
// Function Prototypes
//==============================================================================

// Initialization and cleanup
int bm1398_init(bm1398_context_t *ctx);
void bm1398_cleanup(bm1398_context_t *ctx);

// FPGA indirect register access (matches bmminer/factory test)
uint32_t fpga_read_indirect(bm1398_context_t *ctx, int logical_index);
void fpga_write_indirect(bm1398_context_t *ctx, int logical_index, uint32_t value);

// Low-level UART commands
uint8_t bm1398_crc5(const uint8_t *data, unsigned int bits);
int bm1398_send_uart_cmd(bm1398_context_t *ctx, int chain,
                         const uint8_t *cmd, size_t len);

// Chain control
int bm1398_chain_inactive(bm1398_context_t *ctx, int chain);
int bm1398_set_chip_address(bm1398_context_t *ctx, int chain, uint8_t addr);
int bm1398_enumerate_chips(bm1398_context_t *ctx, int chain, int num_chips);

// Hardware reset control (FPGA register 0x034)
void bm1398_chain_reset_low(bm1398_context_t *ctx, int chain);
void bm1398_chain_reset_high(bm1398_context_t *ctx, int chain);
int bm1398_hardware_reset_chain(bm1398_context_t *ctx, int chain);

// Register operations
int bm1398_write_register(bm1398_context_t *ctx, int chain, bool broadcast,
                          uint8_t chip_addr, uint8_t reg_addr, uint32_t value);
int bm1398_read_register(bm1398_context_t *ctx, int chain, bool broadcast,
                         uint8_t chip_addr, uint8_t reg_addr, uint32_t *value,
                         int timeout_ms);
int bm1398_read_modify_write_register(bm1398_context_t *ctx, int chain,
                                      uint8_t reg_addr, uint32_t clear_mask,
                                      uint32_t set_mask);

// Chain initialization
int bm1398_reset_chain_stage1(bm1398_context_t *ctx, int chain);
int bm1398_configure_chain_stage2(bm1398_context_t *ctx, int chain,
                                  uint8_t diode_vdd_mux_sel);
int bm1398_init_chain(bm1398_context_t *ctx, int chain);

// Baud rate and frequency configuration
int bm1398_set_baud_rate(bm1398_context_t *ctx, int chain, uint32_t baud_rate);
int bm1398_set_frequency(bm1398_context_t *ctx, int chain, uint32_t freq_mhz);

// Work submission
int bm1398_enable_work_send(bm1398_context_t *ctx);
int bm1398_start_work_gen(bm1398_context_t *ctx);
int bm1398_check_work_fifo_ready(bm1398_context_t *ctx, int chain);
int bm1398_set_ticket_mask(bm1398_context_t *ctx, int chain, uint32_t mask);
int bm1398_send_work(bm1398_context_t *ctx, int chain, uint32_t work_id,
                    const uint8_t *work_data_12bytes,
                    const uint8_t midstates[4][32]);

// Nonce collection
int bm1398_get_nonce_count(bm1398_context_t *ctx);
int bm1398_read_nonce(bm1398_context_t *ctx, nonce_response_t *nonce);
int bm1398_read_nonces(bm1398_context_t *ctx, nonce_response_t *nonces,
                      int max_count);

// Utility functions
uint32_t bm1398_detect_chains(bm1398_context_t *ctx);
int bm1398_get_crc_error_count(bm1398_context_t *ctx);
int gpio_setup(int gpio, int value);

// PSU and hashboard power control
int bm1398_psu_power_on(bm1398_context_t *ctx, uint32_t voltage_mv);
int bm1398_psu_set_voltage(bm1398_context_t *ctx, uint32_t voltage_mv);
int bm1398_enable_dc_dc(bm1398_context_t *ctx, int chain);

#endif // BM1398_ASIC_H
