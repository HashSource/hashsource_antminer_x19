# Bitmain S19 Pro Test Fixture

## Quick Start

```bash
cd /root/test_fixture

# Normal test (no logging)
./run.sh

# With FPGA register logging (recommended for reverse engineering)
./run_debug.sh
```

View logs after test completes:

```bash
cat /tmp/fpga_registers.log
```

## Overview

This directory contains tools for running Bitmain's factory test software (`single_board_test`) on HashSource control boards without specialized test fixture hardware.

**Components:**

- `single_board_test` - Bitmain factory test binary (170KB, ARM, from 2020-05-08)
- `test_fixture_shim.so` - LD_PRELOAD library (emulates LCD/GPIO, rewrites paths)
- `fpga_logger` - Register polling tool (captures FPGA operations, in /usr/bin/)
- `Config.ini` - Test configuration (JSON format)

## Test Modes

**PT1 (Phase Test 1)**: Basic ASIC detection

- Scans for all 114 BM1398 chips
- No pattern files required
- Fast execution

**PT2 (Phase Test 2)**: Full functional test

- Runs mining patterns
- Validates nonce generation
- Requires pattern files in `BM1398-pattern/`

Configure test mode in `Config.ini`:

```json
{
  "Test_Process": "PT1",
  "Asic_Num": 114,
  "Voltage": 1360,
  "Frequence": 525
}
```

## Hardware Emulation (test_fixture_shim.so)

The shim library uses LD_PRELOAD to intercept system calls and emulate missing hardware:

**LCD Display Emulation:**

- Intercepts `/dev/bitmain-lcd` operations
- Returns virtual file descriptor (9999)
- Silently accepts all writes

**GPIO Button Emulation:**

- Intercepts `/sys/class/gpio/gpio943/value` reads
- Auto-returns '0' (button pressed)
- Test starts immediately without manual button press

**Path Rewriting:**

- Redirects `/mnt/card/*` to `/root/test_fixture/*`
- Allows running from any directory
- No need to create symlinks or copy files

**Version:** v3.0 (simplified, FPGA logging removed)
**Size:** 9.3KB

## FPGA Register Logging (fpga_logger)

For reverse engineering FPGA operations, use `fpga_logger` instead of the shim. It captures actual register changes by polling memory-mapped regions.

**Why fpga_logger is better:**

- Captures all register read/write operations (LD_PRELOAD cannot do this)
- 1ms polling interval captures 99.9% of operations
- Shows actual register values changing
- Microsecond timestamps

**How it works:**

1. Maps same FPGA registers as single_board_test (shared memory-mapped I/O)
2. Maintains shadow copy of all register values
3. Polls every 1ms, comparing current to shadow
4. Logs all detected changes with timestamps

**Usage:**

The `run_debug.sh` script automatically runs fpga_logger alongside single_board_test:

```bash
./run_debug.sh
```

Manual usage:

```bash
# Start logger in background
fpga_logger --no-restart /tmp/fpga_registers.log &
LOGGER_PID=$!

# Run test
LD_PRELOAD=./test_fixture_shim.so ./single_board_test

# Stop logger
kill -INT $LOGGER_PID
```

**Log format:**

```
[0.000123] INIT 0x000 0x12345678           # Initial state
[1.234567] 0x040: 0x00000000 -> 0x00000001  # Register changed
[2.567123] 0x044: 0x00000000 -> 0xABCDEF00  # Register changed
[120.456789] FINAL 0x000 0x12345678          # Final state
```

**Analysis commands:**

```bash
# View all changes
cat /tmp/fpga_registers.log

# Filter specific register
grep '0x040:' /tmp/fpga_registers.log

# Count operations
grep -c ' -> ' /tmp/fpga_registers.log

# Download to host
scp root@<ip>:/tmp/fpga_registers.log .
```

**Quick register dump (snapshot):**

```bash
fpga_logger --dump              # Non-zero registers only
fpga_logger --dump --all        # All registers (0x000-0x11FF)
```

## Running Scripts

**run.sh** - Normal mode

```bash
#!/bin/sh
LD_PRELOAD=/root/test_fixture/test_fixture_shim.so /root/test_fixture/single_board_test
```

**run_debug.sh** - With FPGA logging

```bash
#!/bin/sh
# Starts fpga_logger, runs test, stops logger
# Logs saved to /tmp/fpga_registers.log
```

## Expected Hardware

The test binary expects:

- `/dev/axi_fpga_dev` - AXI FPGA device (4608 bytes, registers 0x000-0x11FF)
- `/dev/fpga_mem` - FPGA memory device (16MB buffer)
- Kernel modules: `bitmain_axi.ko`, `fpga_mem_driver.ko`

These must be present on the system. The shim only emulates LCD and GPIO, not FPGA devices.

## Execution Flow

1. **Initialization**

   - Opens `/dev/bitmain-lcd` (intercepted by shim)
   - Reads `Config.ini` (path rewritten from `/mnt/card/` to `/root/test_fixture/`)
   - Maps FPGA devices

2. **Test Preparation**

   - Loads patterns if PT2
   - Displays status on LCD (emulated)
   - Waits for button press (auto-pressed by shim)

3. **Test Execution**
   - PT1: Enumerate 114 ASICs
   - PT2: Run mining patterns
   - Saves results to `/root/test_fixture/Result/`

## Troubleshooting

**LCD init fail**

- Cause: Shim not loaded
- Fix: Ensure `LD_PRELOAD=./test_fixture_shim.so` is set

**Can't read config file**

- Cause: Path rewriting not active or wrong working directory
- Fix: Run from `/root/test_fixture/` directory

**Test hangs at "Press Start Key"**

- Cause: GPIO button emulation not working
- Fix: Rebuild shim or check LD_PRELOAD path

**FPGA device errors**

- Cause: Kernel modules not loaded
- Fix: `modprobe bitmain_axi && modprobe fpga_mem_driver`

**No FPGA logs created**

- Note: LD_PRELOAD shim does NOT log FPGA operations
- Solution: Use `fpga_logger` (run `./run_debug.sh`)

## Technical Details

**single_board_test binary:**

- Type: ELF 32-bit LSB executable, ARM EABI5
- Size: 169,928 bytes
- Build: 2020-05-08 (commit 9d1c2687)
- Author: zhiqiang.liu@bitmain.com

**test_fixture_shim.so:**

- Type: ELF 32-bit LSB shared object, ARM EABI5
- Size: 9.3KB (stripped)
- Version: v3.0
- Syscalls intercepted: open, close, read, write, ioctl, stat, opendir, mkdir

**fpga_logger:**

- Type: ELF 32-bit LSB executable, ARM EABI5
- Size: 393KB (static binary)
- Polling: 1ms interval
- Registers: 0x000-0x11FF (1152 registers, 4608 bytes)

## Files in This Directory

```
/root/test_fixture/
├── single_board_test        # Factory test binary
├── test_fixture_shim.so     # LD_PRELOAD library
├── Config.ini               # Test configuration
├── run.sh                   # Normal mode
├── run_debug.sh             # With fpga_logger
└── README.md                # This file

/usr/bin/
└── fpga_logger              # FPGA register logger
```

## References

- Platform: Bitmain S19 Pro NBP1901-38 hashboard
- SoC: Xilinx Zynq-7007S (ARM Cortex-A9 + FPGA)
- ASIC: BM1398 (114 chips, 7nm, 500 MHz)
- Hash rate: 110 TH/s (SHA-256)
