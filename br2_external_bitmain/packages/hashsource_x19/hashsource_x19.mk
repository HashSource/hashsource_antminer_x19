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
endef

$(eval $(generic-package))
