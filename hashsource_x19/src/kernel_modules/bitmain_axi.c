/*
 * bitmain_axi.c - FPGA AXI Register Access Driver with Debug Logging
 *
 * Creates /dev/axi_fpga_dev character device that allows userspace to mmap
 * FPGA registers at physical address 0x40000000 (size 5120 bytes)
 *
 * Reimplemented from Bitmain stock driver with extensive debug logging
 * to trace all register access from single_board_test and bmminer
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/mm.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/slab.h>

#define DEVICE_NAME "axi_fpga_dev"
#define CLASS_NAME  "axi_fpga_dev"  /* Must match stock driver */

#define FPGA_PHYS_ADDR 0x40000000  /* Physical address of FPGA registers */
#define FPGA_SIZE      0x1400       /* 5120 bytes */

/* Global variables (match original driver) */
static dev_t axi_fpga_dev_num;
static struct cdev *p_axi_fpga_dev;
static struct class *axi_fpga_class;
static void __iomem *base_vir_addr;      /* ioremap'd virtual address */
static void *base_vir_mem_addr;          /* Memory region structure */

/* Debug: Track mmap operations */
static atomic_t mmap_count = ATOMIC_INIT(0);

/* File operations */
static int axi_fpga_dev_open(struct inode *inode, struct file *filp)
{
    pr_info("[AXI_FPGA] open() called by PID %d (%s)\n",
            current->pid, current->comm);
    return 0;
}

static int axi_fpga_dev_release(struct inode *inode, struct file *filp)
{
    pr_info("[AXI_FPGA] release() called by PID %d (%s)\n",
            current->pid, current->comm);
    return 0;
}

static int axi_fpga_dev_mmap(struct file *filp, struct vm_area_struct *vma)
{
    unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
    unsigned long size = vma->vm_end - vma->vm_start;
    unsigned long pfn = FPGA_PHYS_ADDR >> PAGE_SHIFT;
    int mmap_id = atomic_inc_return(&mmap_count);

    pr_info("[AXI_FPGA] mmap() #%d called by PID %d (%s)\n",
            mmap_id, current->pid, current->comm);
    pr_info("[AXI_FPGA]   VMA: 0x%lx - 0x%lx (size 0x%lx)\n",
            vma->vm_start, vma->vm_end, size);
    pr_info("[AXI_FPGA]   Offset: 0x%lx, PFN: 0x%lx\n", offset, pfn);
    pr_info("[AXI_FPGA]   Physical addr: 0x%08x, size: 0x%x\n",
            FPGA_PHYS_ADDR, FPGA_SIZE);

    /* Set memory attributes - uncached, shared (matches original) */
    vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
    vma->vm_flags |= VM_IO | VM_DONTEXPAND | VM_DONTDUMP;

    pr_info("[AXI_FPGA]   Page protection: 0x%lx\n",
            pgprot_val(vma->vm_page_prot));

    /* Map physical FPGA memory to userspace */
    if (remap_pfn_range(vma, vma->vm_start, pfn, size, vma->vm_page_prot)) {
        pr_err("[AXI_FPGA] ERROR: remap_pfn_range failed!\n");
        return -EAGAIN;
    }

    pr_info("[AXI_FPGA] mmap() #%d completed successfully\n", mmap_id);
    return 0;
}

static const struct file_operations axi_fpga_dev_fops = {
    .owner   = THIS_MODULE,
    .open    = axi_fpga_dev_open,
    .release = axi_fpga_dev_release,
    .mmap    = axi_fpga_dev_mmap,
};

/* Module initialization */
static int __init axi_fpga_dev_init(void)
{
    int ret;
    struct device *dev;

    pr_info("[AXI_FPGA] ======================================\n");
    pr_info("[AXI_FPGA] Initializing driver (DEBUG VERSION)\n");
    pr_info("[AXI_FPGA] FPGA Physical Address: 0x%08x\n", FPGA_PHYS_ADDR);
    pr_info("[AXI_FPGA] FPGA Size: %d bytes (0x%x)\n", FPGA_SIZE, FPGA_SIZE);
    pr_info("[AXI_FPGA] ======================================\n");

    /* Allocate character device number */
    ret = alloc_chrdev_region(&axi_fpga_dev_num, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        pr_err("[AXI_FPGA] ERROR: Failed to allocate chrdev region: %d\n", ret);
        return ret;
    }
    pr_info("[AXI_FPGA] Allocated chrdev region: major %d, minor %d\n",
            MAJOR(axi_fpga_dev_num), MINOR(axi_fpga_dev_num));

    /* Allocate and initialize cdev structure */
    p_axi_fpga_dev = cdev_alloc();
    if (!p_axi_fpga_dev) {
        pr_err("[AXI_FPGA] ERROR: Failed to allocate cdev\n");
        ret = -ENOMEM;
        goto fail_cdev_alloc;
    }
    pr_info("[AXI_FPGA] Allocated cdev structure\n");

    cdev_init(p_axi_fpga_dev, &axi_fpga_dev_fops);
    p_axi_fpga_dev->owner = THIS_MODULE;

    /* Add character device to system */
    ret = cdev_add(p_axi_fpga_dev, axi_fpga_dev_num, 1);
    if (ret < 0) {
        pr_err("[AXI_FPGA] ERROR: Failed to add cdev: %d\n", ret);
        goto fail_cdev_add;
    }
    pr_info("[AXI_FPGA] Added cdev to system\n");

    /* Request I/O memory region */
    base_vir_mem_addr = request_mem_region(FPGA_PHYS_ADDR, FPGA_SIZE,
                                          "axi_fpga_vir_mem");
    if (!base_vir_mem_addr) {
        pr_err("[AXI_FPGA] ERROR: request_mem_region failed!\n");
        pr_err("[AXI_FPGA]   Region 0x%08x-0x%08x may be in use\n",
               FPGA_PHYS_ADDR, FPGA_PHYS_ADDR + FPGA_SIZE - 1);
        ret = -EBUSY;
        goto fail_mem_region;
    }
    pr_info("[AXI_FPGA] Reserved I/O memory region 0x%08x-0x%08x\n",
            FPGA_PHYS_ADDR, FPGA_PHYS_ADDR + FPGA_SIZE - 1);

    /* Map physical memory to kernel virtual address space */
    base_vir_addr = ioremap(FPGA_PHYS_ADDR, FPGA_SIZE);
    if (!base_vir_addr) {
        pr_err("[AXI_FPGA] ERROR: ioremap failed!\n");
        ret = -ENOMEM;
        goto fail_ioremap;
    }
    pr_info("[AXI_FPGA] Mapped FPGA registers to kernel virtual address 0x%p\n",
            base_vir_addr);

    /* Read first FPGA register (stock driver does this) */
    {
        uint32_t reg_val;
        mb();  /* Memory barrier before read */
        reg_val = ioread32(base_vir_addr);
        mb();  /* Memory barrier after read */
        pr_info("[AXI_FPGA] First FPGA register value: 0x%08x\n", reg_val);
    }

    /* Create device class */
    axi_fpga_class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(axi_fpga_class)) {
        pr_err("[AXI_FPGA] ERROR: Failed to create device class\n");
        ret = PTR_ERR(axi_fpga_class);
        goto fail_class;
    }
    pr_info("[AXI_FPGA] Created device class '%s'\n", CLASS_NAME);

    /* Create device node /dev/axi_fpga_dev */
    dev = device_create(axi_fpga_class, NULL, axi_fpga_dev_num, NULL, DEVICE_NAME);
    if (IS_ERR(dev)) {
        pr_err("[AXI_FPGA] ERROR: Failed to create device\n");
        ret = PTR_ERR(dev);
        goto fail_device;
    }
    pr_info("[AXI_FPGA] Created device node /dev/%s\n", DEVICE_NAME);

    pr_info("[AXI_FPGA] ======================================\n");
    pr_info("[AXI_FPGA] Driver initialized successfully!\n");
    pr_info("[AXI_FPGA] Ready to serve mmap() requests\n");
    pr_info("[AXI_FPGA] ======================================\n");

    return 0;

fail_device:
    class_destroy(axi_fpga_class);
fail_class:
    iounmap(base_vir_addr);
fail_ioremap:
    release_mem_region(FPGA_PHYS_ADDR, FPGA_SIZE);
fail_mem_region:
    cdev_del(p_axi_fpga_dev);
fail_cdev_add:
    kfree(p_axi_fpga_dev);
fail_cdev_alloc:
    unregister_chrdev_region(axi_fpga_dev_num, 1);
    return ret;
}

/* Module cleanup */
static void __exit axi_fpga_dev_exit(void)
{
    pr_info("[AXI_FPGA] ======================================\n");
    pr_info("[AXI_FPGA] Removing driver\n");
    pr_info("[AXI_FPGA] Total mmap operations: %d\n",
            atomic_read(&mmap_count));

    device_destroy(axi_fpga_class, axi_fpga_dev_num);
    pr_info("[AXI_FPGA] Destroyed device node\n");

    class_destroy(axi_fpga_class);
    pr_info("[AXI_FPGA] Destroyed device class\n");

    iounmap(base_vir_addr);
    pr_info("[AXI_FPGA] Unmapped kernel virtual address\n");

    release_mem_region(FPGA_PHYS_ADDR, FPGA_SIZE);
    pr_info("[AXI_FPGA] Released I/O memory region\n");

    cdev_del(p_axi_fpga_dev);
    pr_info("[AXI_FPGA] Removed cdev\n");

    kfree(p_axi_fpga_dev);
    unregister_chrdev_region(axi_fpga_dev_num, 1);
    pr_info("[AXI_FPGA] Unregistered chrdev region\n");

    pr_info("[AXI_FPGA] Driver removed successfully\n");
    pr_info("[AXI_FPGA] ======================================\n");
}

module_init(axi_fpga_dev_init);
module_exit(axi_fpga_dev_exit);

MODULE_AUTHOR("HashSource (reimplemented from Bitmain)");
MODULE_DESCRIPTION("FPGA AXI Register Access Driver with Debug Logging");
MODULE_VERSION("1.0-debug");
