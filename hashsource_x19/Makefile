# HashSource X19 Mining Software Makefile

# Cross-compilation settings for ARM
# Use buildroot toolchain (Linaro GCC 7.2-2017.11) to match stock kernel modules
BUILDROOT_HOST := $(realpath ../buildroot/output/host)
ifneq ($(wildcard $(BUILDROOT_HOST)/bin/arm-linux-gnueabihf-gcc),)
	CROSS_COMPILE ?= $(BUILDROOT_HOST)/bin/arm-linux-gnueabihf-
else
	$(warning WARNING: Buildroot toolchain not found, using system toolchain)
	CROSS_COMPILE ?= arm-linux-gnueabihf-
endif

CC = $(CROSS_COMPILE)gcc
AR = $(CROSS_COMPILE)ar
STRIP = $(CROSS_COMPILE)strip

# Directories
SRC_DIR = src
INC_DIR = include
DRV_DIR = drivers
OBJ_DIR = obj
BIN_DIR = bin
KMOD_DIR = $(SRC_DIR)/kernel_modules

# Target binaries
TARGET = $(BIN_DIR)/hashsource_miner
FAN_TEST = $(BIN_DIR)/fan_test
FPGA_LOGGER = $(BIN_DIR)/fpga_logger
PSU_TEST = $(BIN_DIR)/psu_test
ID2MAC = $(BIN_DIR)/id2mac
EEPROM_DETECT = $(BIN_DIR)/eeprom_detect
CHAIN_TEST = $(BIN_DIR)/chain_test
WORK_TEST = $(BIN_DIR)/work_test
PATTERN_TEST = $(BIN_DIR)/pattern_test
PATTERN_PARSER = $(BIN_DIR)/pattern_parser
TEST_FIXTURE_SHIM = $(BIN_DIR)/test_fixture_shim.so

# Source files for main miner
SRCS = $(SRC_DIR)/main.c

# Source files for fan test
FAN_SRCS = $(SRC_DIR)/fan_test.c

# Source files for FPGA logger
LOGGER_SRCS = $(SRC_DIR)/fpga_logger.c

# Source files for PSU test
PSU_SRCS = $(SRC_DIR)/psu_test.c

# Source files for id2mac
ID2MAC_SRCS = $(SRC_DIR)/id2mac.c

# Source files for eeprom_detect
EEPROM_DETECT_SRCS = $(SRC_DIR)/eeprom_detect.c

# Source files for chain_test (includes BM1398 driver)
CHAIN_TEST_SRCS = $(SRC_DIR)/chain_test.c $(SRC_DIR)/bm1398_asic.c

# Source files for work_test (includes BM1398 driver)
WORK_TEST_SRCS = $(SRC_DIR)/work_test.c $(SRC_DIR)/bm1398_asic.c

# Source files for pattern_test (includes BM1398 driver)
PATTERN_TEST_SRCS = $(SRC_DIR)/pattern_test.c $(SRC_DIR)/bm1398_asic.c

# Source files for pattern_parser
PATTERN_PARSER_SRCS = $(SRC_DIR)/pattern_parser.c

# Source files for test fixture shim
TEST_FIXTURE_SHIM_SRCS = $(SRC_DIR)/test_fixture_shim.c

# Object files
OBJS = $(patsubst %.c,$(OBJ_DIR)/%.o,$(notdir $(SRCS)))
FAN_OBJS = $(patsubst %.c,$(OBJ_DIR)/%.o,$(notdir $(FAN_SRCS)))
LOGGER_OBJS = $(patsubst %.c,$(OBJ_DIR)/%.o,$(notdir $(LOGGER_SRCS)))
PSU_OBJS = $(patsubst %.c,$(OBJ_DIR)/%.o,$(notdir $(PSU_SRCS)))
ID2MAC_OBJS = $(patsubst %.c,$(OBJ_DIR)/%.o,$(notdir $(ID2MAC_SRCS)))
EEPROM_DETECT_OBJS = $(patsubst %.c,$(OBJ_DIR)/%.o,$(notdir $(EEPROM_DETECT_SRCS)))
CHAIN_TEST_OBJS = $(patsubst %.c,$(OBJ_DIR)/%.o,$(notdir $(CHAIN_TEST_SRCS)))
WORK_TEST_OBJS = $(patsubst %.c,$(OBJ_DIR)/%.o,$(notdir $(WORK_TEST_SRCS)))
PATTERN_TEST_OBJS = $(patsubst %.c,$(OBJ_DIR)/%.o,$(notdir $(PATTERN_TEST_SRCS)))
PATTERN_PARSER_OBJS = $(patsubst %.c,$(OBJ_DIR)/%.o,$(notdir $(PATTERN_PARSER_SRCS)))

# Compiler flags
CFLAGS = -Wall -Wextra -O2 -g
CFLAGS += -I$(INC_DIR)
CFLAGS += -march=armv7-a -mfpu=neon -mfloat-abi=hard
CFLAGS += -D_GNU_SOURCE

# Linker flags
LDFLAGS = -pthread -lm -lrt

# Kernel module settings
# KERNEL_SRC points to Linux 4.6.0-xilinx source (used by stock S19 Pro firmware)
# ARCH is always arm for Xilinx Zynq-7007 on S19 Pro
KERNEL_SRC ?= $(realpath ../buildroot/output/build/linux-custom)
ARCH = arm

# Kernel modules to build
KERNEL_MODULES = bitmain_axi.ko fpga_mem_driver.ko

# Default target
all: dirs $(TARGET) $(FAN_TEST) $(FPGA_LOGGER) $(PSU_TEST) $(ID2MAC) $(EEPROM_DETECT) $(CHAIN_TEST) $(WORK_TEST) $(PATTERN_TEST) $(PATTERN_PARSER) $(TEST_FIXTURE_SHIM)

# Create directories
dirs:
	@mkdir -p $(OBJ_DIR) $(BIN_DIR)

# Build main target
$(TARGET): $(OBJS)
	@echo "Linking $@"
	$(CC) $(OBJS) -o $@ $(LDFLAGS)
	@echo "Stripping $@"
	$(STRIP) $@
	@echo "Build complete: $@"

# Build fan test
$(FAN_TEST): $(FAN_OBJS)
	@echo "Linking $@"
	$(CC) $(FAN_OBJS) -o $@ $(LDFLAGS)
	@echo "Stripping $@"
	$(STRIP) $@
	@echo "Build complete: $@"

# Build FPGA logger (static for portability)
$(FPGA_LOGGER): $(LOGGER_OBJS)
	@echo "Linking $@ (static)"
	$(CC) $(LOGGER_OBJS) -o $@ $(LDFLAGS) -static
	@echo "Stripping $@"
	$(STRIP) $@
	@echo "Build complete: $@"

# Build PSU test
$(PSU_TEST): $(PSU_OBJS)
	@echo "Linking $@"
	$(CC) $(PSU_OBJS) -o $@ $(LDFLAGS)
	@echo "Stripping $@"
	$(STRIP) $@
	@echo "Build complete: $@"

# Build id2mac
$(ID2MAC): $(ID2MAC_OBJS)
	@echo "Linking $@"
	$(CC) $(ID2MAC_OBJS) -o $@
	@echo "Stripping $@"
	$(STRIP) $@
	@echo "Build complete: $@"

# Build eeprom_detect
$(EEPROM_DETECT): $(EEPROM_DETECT_OBJS)
	@echo "Linking $@"
	$(CC) $(EEPROM_DETECT_OBJS) -o $@ $(LDFLAGS)
	@echo "Stripping $@"
	$(STRIP) $@
	@echo "Build complete: $@"

# Build chain_test (includes BM1398 driver)
$(CHAIN_TEST): $(CHAIN_TEST_OBJS)
	@echo "Linking $@"
	$(CC) $(CHAIN_TEST_OBJS) -o $@ $(LDFLAGS)
	@echo "Stripping $@"
	$(STRIP) $@
	@echo "Build complete: $@"

# Build work_test (includes BM1398 driver)
$(WORK_TEST): $(WORK_TEST_OBJS)
	@echo "Linking $@"
	$(CC) $(WORK_TEST_OBJS) -o $@ $(LDFLAGS)
	@echo "Stripping $@"
	$(STRIP) $@
	@echo "Build complete: $@"

# Build pattern_test (includes BM1398 driver)
$(PATTERN_TEST): $(PATTERN_TEST_OBJS)
	@echo "Linking $@"
	$(CC) $(PATTERN_TEST_OBJS) -o $@ $(LDFLAGS)
	@echo "Stripping $@"
	$(STRIP) $@
	@echo "Build complete: $@"

# Build pattern_parser (standalone utility)
$(PATTERN_PARSER): $(PATTERN_PARSER_OBJS)
	@echo "Linking $@"
	$(CC) $(PATTERN_PARSER_OBJS) -o $@
	@echo "Stripping $@"
	$(STRIP) $@
	@echo "Build complete: $@"

# Build test fixture shim (shared library for LD_PRELOAD)
$(TEST_FIXTURE_SHIM): dirs $(TEST_FIXTURE_SHIM_SRCS)
	@echo "Compiling test_fixture_shim.so..."
	$(CC) -shared -fPIC -o $@ $(TEST_FIXTURE_SHIM_SRCS) -ldl -O2 -Wall
	@echo "Stripping $@"
	$(STRIP) $@
	@echo "Build complete: $@"

# Compile source files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@echo "Compiling $<"
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: $(DRV_DIR)/%.c
	@echo "Compiling $<"
	$(CC) $(CFLAGS) -c $< -o $@

# Build kernel modules (if KERNEL_SRC is available)
modules:
	@echo "========================================"
	@echo "Building kernel modules"
	@echo "========================================"
	@if [ ! -d "$(KERNEL_SRC)" ]; then \
		echo "ERROR: Kernel source not found at: $(KERNEL_SRC)"; \
		echo "Please set KERNEL_SRC to point to Linux 4.6.0-xilinx source"; \
		exit 1; \
	fi
	@echo "Kernel source: $(KERNEL_SRC)"
	@echo "Cross-compiler: $(CROSS_COMPILE)gcc"
	@$(CROSS_COMPILE)gcc --version | head -n1
	@echo "Architecture: $(ARCH)"
	@echo ""
	@echo "Building modules..."
	$(MAKE) -C $(KERNEL_SRC) M=$(realpath $(KMOD_DIR)) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules
	@echo ""
	@echo "Patching module CRCs to match bitmain kernel..."
	@if [ ! -d "$(KMOD_DIR)/bitmain" ] || [ ! -f "$(KMOD_DIR)/bitmain/bitmain_axi.ko" ]; then \
		echo "ERROR: Bitmain modules not found in $(KMOD_DIR)/bitmain/"; \
		echo "Please copy bitmain modules from Bitmain machine first:"; \
		exit 1; \
	fi
	@cd $(KMOD_DIR) && \
		python3 patch_module_crcs.py bitmain_axi.ko bitmain/bitmain_axi.ko bitmain_axi.ko.patched && \
		python3 patch_module_crcs.py fpga_mem_driver.ko bitmain/fpga_mem_driver.ko fpga_mem_driver.ko.patched && \
		mv bitmain_axi.ko.patched bitmain_axi.ko && \
		mv fpga_mem_driver.ko.patched fpga_mem_driver.ko
	@echo ""
	@echo "Kernel modules built and patched successfully:"
	@for mod in $(KERNEL_MODULES); do \
		if [ -f "$(KMOD_DIR)/$$mod" ]; then \
			echo "  $$mod"; \
			modinfo $(KMOD_DIR)/$$mod | grep -E "^(filename|vermagic|version|description):" | sed 's/^/    /'; \
		else \
			echo "  $$mod - FAILED"; \
		fi; \
	done
	@echo "========================================"

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts"
	rm -rf $(OBJ_DIR) $(BIN_DIR)

# Clean kernel modules
modules-clean:
	@echo "Cleaning kernel modules..."
	@if [ -d "$(KMOD_DIR)" ]; then \
		rm -f $(KMOD_DIR)/*.o $(KMOD_DIR)/*.ko $(KMOD_DIR)/*.mod.c $(KMOD_DIR)/*.mod.o; \
		rm -f $(KMOD_DIR)/*.order $(KMOD_DIR)/*.symvers $(KMOD_DIR)/.*.cmd; \
		rm -rf $(KMOD_DIR)/.tmp_versions; \
		echo "Kernel modules cleaned"; \
	fi

# Clean everything including kernel modules
distclean: clean modules-clean

# Install to target filesystem
install: $(TARGET)
	@echo "Installing to target filesystem"
	cp $(TARGET) ../buildroot/output/target/usr/bin/
	cp config/miner.conf ../buildroot/output/target/etc/

# Help target
help:
	@echo "HashSource X19 Mining Software Build System"
	@echo "=============================================="
	@echo ""

.PHONY: all dirs clean modules modules-clean modules-install distclean install config startup help
