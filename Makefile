# X19 Firmware - Top Level Makefile
BUILDROOT_DIR := buildroot
EXTERNAL_DIR := br2_external_bitmain
BR2_EXTERNAL := $(CURDIR)/$(EXTERNAL_DIR)

# Use all CPU cores by default if -j is not specified
ifeq ($(filter -j%, $(MAKEFLAGS)),)
MAKEFLAGS += -j$(shell nproc)
endif

.PHONY: all menuconfig clean distclean help

all:
	@$(MAKE) $(MAKEFLAGS) -C $(BUILDROOT_DIR) BR2_EXTERNAL=$(BR2_EXTERNAL)

menuconfig:
	@$(MAKE) -C $(BUILDROOT_DIR) BR2_EXTERNAL=$(BR2_EXTERNAL) menuconfig

# Ramdisk-only build
x19_xilinx_ramdisk_defconfig:
	@$(MAKE) -C $(BUILDROOT_DIR) BR2_EXTERNAL=$(BR2_EXTERNAL) x19_xilinx_ramdisk_defconfig

busybox-menuconfig:
	@$(MAKE) -C $(BUILDROOT_DIR) BR2_EXTERNAL=$(BR2_EXTERNAL) busybox-menuconfig

hashsource_x19-rebuild:
	@$(MAKE) -C $(BUILDROOT_DIR) BR2_EXTERNAL=$(BR2_EXTERNAL) hashsource_x19-rebuild

hashsource_x19-reconfigure:
	@$(MAKE) -C $(BUILDROOT_DIR) BR2_EXTERNAL=$(BR2_EXTERNAL) hashsource_x19-reconfigure

savedefconfig:
	@$(MAKE) -C $(BUILDROOT_DIR) BR2_EXTERNAL=$(BR2_EXTERNAL) savedefconfig
	@echo "Defconfig saved to $(EXTERNAL_DIR)/configs/x19_xilinx_ramdisk_defconfig"

clean:
	@$(MAKE) -C $(BUILDROOT_DIR) BR2_EXTERNAL=$(BR2_EXTERNAL) clean

distclean:
	@$(MAKE) -C $(BUILDROOT_DIR) BR2_EXTERNAL=$(BR2_EXTERNAL) distclean
