################################################################################
#
# hashsource_x19 (C implementation - BUILD ONLY, do not install)
#
# NOTE: This package builds the C binaries for reference/testing but does
#       NOT install them to the target rootfs. Use hashsource_x19_rs (Rust)
#       for production deployment.
#
################################################################################

HASHSOURCE_X19_VERSION = 1.0
HASHSOURCE_X19_SITE = $(BR2_EXTERNAL_X19_BITMAIN_PATH)/../hashsource_x19
HASHSOURCE_X19_SITE_METHOD = local

define HASHSOURCE_X19_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) CC="$(TARGET_CC)" \
		CROSS_COMPILE="$(TARGET_CROSS)" \
		-C $(@D) all
	@echo "========================================"
	@echo "Building kernel modules with debug logging"
	@echo "========================================"
	-$(TARGET_MAKE_ENV) $(MAKE) CC="$(TARGET_CC)" \
		CROSS_COMPILE="$(TARGET_CROSS)" \
		KERNEL_SRC="$(BUILD_DIR)/linux-headers-custom" \
		ARCH=arm \
		-C $(@D) modules || echo "WARNING: Kernel module build failed (may need kernel source)"
endef

# INSTALL_TARGET_CMDS intentionally left empty - binaries are built but not installed
# to rootfs. This keeps the C code as a reference implementation while using the
# Rust version (hashsource_x19_rs) in production.
#
# To re-enable C binary installation, uncomment the section below:

define HASHSOURCE_X19_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/bin/fan_test \
		$(TARGET_DIR)/usr/bin/fan_test
	$(INSTALL) -D -m 0755 $(@D)/bin/psu_test \
		$(TARGET_DIR)/usr/bin/psu_test
	$(INSTALL) -D -m 0755 $(@D)/bin/fpga_logger \
		$(TARGET_DIR)/usr/bin/fpga_logger
	$(INSTALL) -D -m 0755 $(@D)/bin/eeprom_detect \
		$(TARGET_DIR)/usr/bin/eeprom_detect
	$(INSTALL) -D -m 0755 $(@D)/bin/hashsource_miner \
		$(TARGET_DIR)/usr/bin/hashsource_miner
	$(INSTALL) -D -m 0755 $(@D)/bin/id2mac \
		$(TARGET_DIR)/usr/bin/id2mac
	$(INSTALL) -D -m 0755 $(@D)/bin/chain_test \
		$(TARGET_DIR)/usr/bin/chain_test
	$(INSTALL) -D -m 0755 $(@D)/bin/work_test \
		$(TARGET_DIR)/usr/bin/work_test
	$(INSTALL) -D -m 0755 $(@D)/bin/pattern_test \
		$(TARGET_DIR)/usr/bin/pattern_test
	$(INSTALL) -D -m 0755 $(@D)/bin/pattern_parser \
		$(TARGET_DIR)/usr/bin/pattern_parser
	$(INSTALL) -D -m 0755 $(@D)/bin/test_fixture_shim.so \
		$(TARGET_DIR)/root/test_fixture/test_fixture_shim.so
	if [ -f $(@D)/config/miner.conf ]; then \
		$(INSTALL) -D -m 0644 $(@D)/config/miner.conf \
			$(TARGET_DIR)/etc/miner.conf; \
	fi
	if [ -f $(@D)/config/S90hashsource ]; then \
		$(INSTALL) -D -m 0755 $(@D)/config/S90hashsource \
			$(TARGET_DIR)/etc/init.d/S90hashsource; \
	fi
	@echo "Installing stock Bitmain kernel modules..."
	@mkdir -p $(TARGET_DIR)/lib/modules
	-if [ -f $(@D)/src/kernel_modules/bitmain/bitmain_axi.ko ]; then \
		$(INSTALL) -D -m 0644 $(@D)/src/kernel_modules/bitmain/bitmain_axi.ko \
			$(TARGET_DIR)/lib/modules/bitmain_axi.ko; \
		$(INSTALL) -D -m 0644 $(@D)/src/kernel_modules/bitmain/fpga_mem_driver.ko \
			$(TARGET_DIR)/lib/modules/fpga_mem_driver.ko; \
		echo "Installed stock kernel modules to /lib/modules/"; \
	else \
		echo "WARNING: Stock Bitmain modules not found in src/kernel_modules/bitmain/"; \
	fi
	@echo "Installing debug kernel modules..."
	@mkdir -p $(TARGET_DIR)/lib/modules/debug
	-if [ -f $(@D)/src/kernel_modules/bitmain_axi.ko ]; then \
		$(INSTALL) -D -m 0644 $(@D)/src/kernel_modules/bitmain_axi.ko \
			$(TARGET_DIR)/lib/modules/debug/bitmain_axi.ko; \
		$(INSTALL) -D -m 0644 $(@D)/src/kernel_modules/fpga_mem_driver.ko \
			$(TARGET_DIR)/lib/modules/debug/fpga_mem_driver.ko; \
		echo "Installed debug kernel modules to /lib/modules/debug/"; \
	else \
		echo "WARNING: Debug kernel modules not built, skipping installation"; \
	fi
endef

$(eval $(generic-package))
