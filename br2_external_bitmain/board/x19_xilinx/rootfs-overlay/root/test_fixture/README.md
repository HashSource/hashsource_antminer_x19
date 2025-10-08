# Bitmain S19 Pro Test Fixture Documentation

## Overview

This document covers the Bitmain S19 Pro `single_board_test` manufacturing test binary and the `test_fixture_shim` library that enables running tests over SSH without specialized hardware.

## single_board_test Binary

### Purpose

Manufacturing test fixture software for validating Bitmain S19 Pro hashboards during production. Tests ASIC chips (BM1398) for functionality before final assembly.

### Binary Information

- **Type**: ELF 32-bit LSB executable, ARM EABI5
- **Size**: 169,928 bytes
- **Architecture**: ARM (Xilinx Zynq-7000 SoC)
- **Build**: 2020-05-08 (commit: 9d1c2687fb8abf2f470e9946e65d73b407dcb455)
- **Author**: zhiqiang.liu@bitmain.com

### Hardware Dependencies

The binary expects specific hardware that is normally only available on Bitmain factory test fixtures:

1. **LCD Display**: `/dev/bitmain-lcd` (12864 LCD, 4×16 character display)

   - Driver: `lcd12864I_driver.ko`
   - Shows test status and prompts

2. **FPGA Devices**:

   - `/dev/axi_fpga_dev` (Bitmain AXI bus driver)
   - `/dev/fpga_mem` (FPGA memory driver)
   - Used for ASIC communication via SPI

3. **GPIO Button**: GPIO 943 (`/sys/class/gpio/gpio943/value`)

   - "Start Test" button to begin testing
   - Active-low (0 = pressed)

4. **Control Board**: Xilinx Zynq-7000 based controller with custom kernel modules

### Test Modes

The binary supports two test modes configured in `Config.ini`:

#### PT1 (Phase Test 1)

- **Purpose**: Basic ASIC detection
- **Function**: Scans for presence of all ASIC chips
- **Tests**: Chip communication, basic SPI functionality
- **Pattern**: No pattern files needed

#### PT2 (Phase Test 2)

- **Purpose**: Full functional test
- **Function**: Runs mining patterns, validates nonce generation
- **Tests**: Core functionality, hash rate, error rates
- **Pattern**: Requires pattern files in `BM1398-pattern/`

### Configuration

`Config.ini` contains JSON configuration:

```json
{
  "Test_Process": "PT1",
  "Miner_Type": "S19_Pro",
  "Board_Name": "NBP1901-38",
  "Asic_Type": "BM1398",
  "Asic_Num": 114,
  "Voltage_Domain": 38,
  "Asic_Num_Per_Voltage_Domain": 3,
  "Test_Loop": [
    {
      "Level": 1,
      "Pre_Open_Core_Voltage": 1500,
      "Voltage": 1360,
      "Frequence": 525
    }
  ],
  "Factory_Mode": false,
  "Bypass_Scan_Code_Gun": true,
  "Software_Pattern": true
}
```

### Execution Flow

1. **Initialization**

   - Open LCD device (`/dev/bitmain-lcd`)
   - Read configuration (`/mnt/card/Config.ini`)
   - Initialize FPGA devices

2. **Test Preparation**

   - Load test patterns (if PT2)
   - Scan for test standard
   - Display "Press Start Key to Begin Test"

3. **Wait for Button Press**

   - Polls GPIO 943 in loop
   - Continues when button pressed (value = '0')

4. **Execute Test**
   - PT1: Scan and enumerate ASICs
   - PT2: Run mining patterns, validate results
   - Display results on LCD
   - Save results to `/mnt/card/Result/`

### Expected File Locations

The binary expects files at hardcoded paths:

```
/mnt/card/
├── Config.ini              # Configuration file
├── single_board_test       # Binary itself
├── BM1398-pattern/         # Test patterns (PT2 only)
└── Result/                 # Test results output
```

## test_fixture_shim Library

### Purpose

An LD_PRELOAD library that intercepts system calls to emulate missing hardware, allowing `single_board_test` to run on any Linux system without specialized test fixture hardware.

### Features

1. **LCD Display Emulation**

   - Intercepts `/dev/bitmain-lcd` device operations
   - Provides virtual file descriptor (9999)
   - Silently accepts all LCD writes

2. **GPIO Button Emulation**

   - Intercepts `/sys/class/gpio/gpio943/value` reads
   - Returns '0' (button pressed) automatically
   - Bypasses manual button press requirement

3. **Path Rewriting**
   - Transparently redirects filesystem operations
   - Maps: `/mnt/card/*` → `/root/test_fixture/*`
   - Allows running from any directory

### Technical Implementation

**Intercepted System Calls:**

- `open()` / `openat()` / `open64()` - File opening
- `fopen()` / `fopen64()` - Stream opening
- `read()` - GPIO button state
- `write()` - LCD display output
- `close()` - Cleanup virtual devices
- `ioctl()` - LCD control commands
- `access()` / `stat()` / `lstat()` - Path checking
- `opendir()` / `mkdir()` - Directory operations

**Virtual File Descriptors:**

- **9999**: LCD device (`/dev/bitmain-lcd`)
- **9998**: GPIO button (`/sys/class/gpio/gpio943/value`)

### Source Code Structure

```c
// Configuration
#define LCD_DEBUG 0           // Enable LCD activity logging
#define PATH_DEBUG 0          // Enable path rewrite logging
#define LCD_VIRTUAL_FD 9999
#define GPIO_BUTTON_VIRTUAL_FD 9998
#define ORIGINAL_PATH "/mnt/card"
#define REWRITE_PATH "/root/test_fixture"

// State tracking
static int lcd_is_open = 0;
static char lcd_buffer[64];
static int gpio_button_is_open = 0;

// Function pointer initialization via dlsym(RTLD_NEXT, ...)
static int (*real_open)(...) = NULL;
static ssize_t (*real_read)(...) = NULL;
// ... etc

// Path rewriting logic
static const char* rewrite_path(const char *path) {
    if (strncmp(path, "/mnt/card", 9) == 0) {
        // Rewrite to /root/test_fixture
    }
    return path;
}

// Device emulation
int open(const char *pathname, int flags, ...) {
    if (is_lcd_device(pathname)) {
        return LCD_VIRTUAL_FD;
    }
    if (is_start_button(pathname)) {
        return GPIO_BUTTON_VIRTUAL_FD;
    }
    pathname = rewrite_path(pathname);
    return real_open(pathname, flags);
}

ssize_t read(int fd, void *buf, size_t count) {
    if (fd == GPIO_BUTTON_VIRTUAL_FD) {
        // Return '0' = button pressed
        buf[0] = '0';
        buf[1] = '\n';
        return 2;
    }
    return real_read(fd, buf, count);
}
```

### Building

**Requirements:**

- ARM cross-compiler: `arm-linux-gnueabihf-gcc`
- Or: Buildroot toolchain (auto-detected)

**Build Command:**

```bash
./build_test_fixture_shim.sh
```

**Output:**

```
test_fixture_shim.so: ELF 32-bit LSB shared object, ARM, EABI5
Size: ~13KB
```

### Debug Mode

Enable verbose logging by editing `test_fixture_shim.c`:

```c
#define LCD_DEBUG 1      // See LCD writes and GPIO reads
#define PATH_DEBUG 1     // See path rewrites
```

Then rebuild. Output examples:

```
[LCD SHIM] open(/dev/bitmain-lcd) intercepted
[LCD SHIM] write(9999, 64 bytes) - LCD content:
  Row 0: Press Start Key
  Row 1: to Begin Test
[GPIO SHIM] open(/sys/class/gpio/gpio943/value) intercepted
[GPIO SHIM] read(9998, 4) - returning '0' (button pressed)
[PATH REWRITE] /mnt/card/Config.ini -> /root/test_fixture/Config.ini
```

## Usage on Bitmain Control Board

### Setup

1. **Copy files to board:**

```bash
scp test_fixture_shim.so root@<board-ip>:/root/test_fixture/
scp single_board_test root@<board-ip>:/root/test_fixture/
scp Config.ini root@<board-ip>:/root/test_fixture/
scp run_final.sh root@<board-ip>:/root/test_fixture/
```

2. **SSH to board:**

```bash
ssh root@<board-ip>
cd /root/test_fixture
```

3. **Make executable:**

```bash
chmod +x single_board_test run_final.sh
```

### Running Tests

**Method 1: Direct execution**

```bash
LD_PRELOAD=/root/test_fixture/test_fixture_shim.so ./single_board_test
```

**Method 2: Using script**

```bash
./run_final.sh
```

### Expected Output

```
[TEST FIXTURE SHIM] Loaded
  - LCD emulation: /dev/bitmain-lcd active
  - GPIO button emulation: gpio943 (auto-pressed)
  - Path rewrite: /mnt/card -> /root/test_fixture

main : build version information::  version: 9d1c2687fb8abf2f470e9946e65d73b407dcb455
prepare_platform_environment :
parse_local_config_file : Test_Process : PT1
parse_local_config_file : Miner_Type : S19_Pro
parse_local_config_file : Asic_Type : BM1398
parse_local_config_file : Asic_Num : 114
prepare_pattern : PT1 test doesn't need read pattern files
display_main_page : Only find ASIC
prepare_test_process : Will do PT1: Only find ASIC test

[Test proceeds automatically - no button press required]
```

## Troubleshooting

### LCD init fail!!!

**Cause**: LCD device not emulated
**Fix**: Ensure using latest `test_fixture_shim.so` with LD_PRELOAD

### Can't read out local config file: /mnt/card/Config.ini

**Cause**: Path rewriting not active
**Fix**: Verify shim includes path rewriting, check LD_PRELOAD is set

### Test hangs at "Press Start Key"

**Cause**: GPIO button emulation not working
**Fix**: Rebuild shim with GPIO support, ensure GPIO_BUTTON_VIRTUAL_FD defined

### FPGA device errors

**Expected**: The shim doesn't emulate FPGA devices yet
**Impact**: Tests requiring actual ASIC communication will fail
**Workaround**: Extend shim to emulate `/dev/axi_fpga_dev` and `/dev/fpga_mem`

## Architecture Notes

### Why LD_PRELOAD?

LD_PRELOAD allows runtime interception of library calls without:

- Modifying the binary (maintains original functionality)
- Requiring kernel modules (runs in userspace)
- Needing source code (works with closed-source binaries)
- Complex patching (simple C library)

### Security Considerations

**Vulnerabilities in single_board_test:**

1. Buffer overflows in string parsing
2. No input validation on config files
3. Command injection via system() calls
4. No encryption for network operations
5. Hardcoded credentials in some variants

**Note**: This is manufacturing test software, not production firmware. Use only in controlled test environments.

## File Reference

```
/home/danielsokil/Downloads/Bitmain_Test_Fixtures/S19_Pro/
├── single_board_test           # Test binary (ARM executable)
├── Config.ini                  # Configuration (JSON)
├── test_fixture_shim.c         # Shim library source (423 lines)
├── test_fixture_shim.so        # Compiled shim (ARM shared object)
├── build_test_fixture_shim.sh  # Build script
├── run_final.sh                # Execution wrapper
└── DOCUMENTATION.md            # This file
```

## Version Information

- **single_board_test**: 9d1c2687fb (2020-05-08)
- **test_fixture_shim**: v1.1 (GPIO button support added 2025-10-08)
- **Target Platform**: Bitmain S19 Pro NBP1901-38 hashboard
- **ASIC Chip**: BM1398 (114 chips per board)

## References

- Bitmain S19 Pro: 110 TH/s Bitcoin miner (SHA-256)
- BM1398: 7nm ASIC chip, ~500 MHz operating frequency
- Xilinx Zynq-7000: ARM Cortex-A9 + FPGA fabric
- Test fixture ramdisk: Linux 4.19.0-xilinx-g31c3210
