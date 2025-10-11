/*
 * fpga_mem_driver.c - FPGA Memory Access Driver with Debug Logging
 *
 * Creates /dev/fpga_mem character device that allows userspace to mmap
 * FPGA memory at a configurable physical address offset
 *
 * Module parameter: fpga_mem_offset_addr (default: 0x0F000000 for 256MB RAM)
 *
 * Reimplemented from Bitmain stock driver with extensive debug logging
 * to trace all memory access from single_board_test and bmminer
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

#define DEVICE_NAME "fpga_mem"
#define CLASS_NAME  "fpga_mem"

#define FPGA_MEM_SIZE 0x1000000  /* 16MB - stock driver size! */

/* Module parameter - FPGA memory offset address */
static int fpga_mem_offset_addr = 0x0F000000;  /* Default for 256MB RAM */
module_param(fpga_mem_offset_addr, int, 0644);
MODULE_PARM_DESC(fpga_mem_offset_addr, "FPGA memory physical address offset");

/* Global variables (match original driver) */
static dev_t fpga_mem_num;
static struct cdev *p_fpga_mem;
static struct class *fpga_mem_class;
static void __iomem *base_vir_addr;      /* ioremap'd virtual address */
static void *base_vir_mem_addr;          /* Memory region structure */

/* Debug: Track mmap operations */
static atomic_t mmap_count = ATOMIC_INIT(0);

/* File operations */
static int fpga_mem_open(struct inode *inode, struct file *filp)
{
    pr_info("[FPGA_MEM] open() called by PID %d (%s)\n",
            current->pid, current->comm);
    return 0;
}

static int fpga_mem_release(struct inode *inode, struct file *filp)
{
    pr_info("[FPGA_MEM] release() called by PID %d (%s)\n",
            current->pid, current->comm);
    return 0;
}

static int fpga_mem_mmap(struct file *filp, struct vm_area_struct *vma)
{
    unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
    unsigned long size = vma->vm_end - vma->vm_start;
    unsigned long pfn = fpga_mem_offset_addr >> PAGE_SHIFT;
    int mmap_id = atomic_inc_return(&mmap_count);

    pr_info("[FPGA_MEM] mmap() #%d called by PID %d (%s)\n",
            mmap_id, current->pid, current->comm);
    pr_info("[FPGA_MEM]   VMA: 0x%lx - 0x%lx (size 0x%lx)\n",
            vma->vm_start, vma->vm_end, size);
    pr_info("[FPGA_MEM]   Offset: 0x%lx, PFN: 0x%lx\n", offset, pfn);
    pr_info("[FPGA_MEM]   Physical addr: 0x%08x, size: 0x%x\n",
            fpga_mem_offset_addr, FPGA_MEM_SIZE);

    /* Set memory attributes - uncached, shared (matches original) */
    vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
    vma->vm_flags |= VM_IO | VM_DONTEXPAND | VM_DONTDUMP;

    pr_info("[FPGA_MEM]   Page protection: 0x%lx\n",
            pgprot_val(vma->vm_page_prot));

    /* Map physical FPGA memory to userspace */
    if (remap_pfn_range(vma, vma->vm_start, pfn, size, vma->vm_page_prot)) {
        pr_err("[FPGA_MEM] ERROR: remap_pfn_range failed!\n");
        return -EAGAIN;
    }

    pr_info("[FPGA_MEM] mmap() #%d completed successfully\n", mmap_id);
    return 0;
}

static const struct file_operations fpga_mem_fops = {
    .owner   = THIS_MODULE,
    .open    = fpga_mem_open,
    .release = fpga_mem_release,
    .mmap    = fpga_mem_mmap,
};

/* Module initialization */
static int __init fpga_mem_init(void)
{
    int ret;
    struct device *dev;

    pr_info("[FPGA_MEM] ======================================\n");
    pr_info("[FPGA_MEM] Initializing driver (DEBUG VERSION)\n");
    pr_info("[FPGA_MEM] FPGA Memory Physical Address: 0x%08x\n", fpga_mem_offset_addr);
    pr_info("[FPGA_MEM] FPGA Memory Size: %d bytes (0x%x)\n", FPGA_MEM_SIZE, FPGA_MEM_SIZE);
    pr_info("[FPGA_MEM] ======================================\n");

    /* Allocate character device number */
    ret = alloc_chrdev_region(&fpga_mem_num, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        pr_err("[FPGA_MEM] ERROR: Failed to allocate chrdev region: %d\n", ret);
        return ret;
    }
    pr_info("[FPGA_MEM] Allocated chrdev region: major %d, minor %d\n",
            MAJOR(fpga_mem_num), MINOR(fpga_mem_num));

    /* Allocate and initialize cdev structure */
    p_fpga_mem = cdev_alloc();
    if (!p_fpga_mem) {
        pr_err("[FPGA_MEM] ERROR: Failed to allocate cdev\n");
        ret = -ENOMEM;
        goto fail_cdev_alloc;
    }
    pr_info("[FPGA_MEM] Allocated cdev structure\n");

    cdev_init(p_fpga_mem, &fpga_mem_fops);
    p_fpga_mem->owner = THIS_MODULE;

    /* Add character device to system */
    ret = cdev_add(p_fpga_mem, fpga_mem_num, 1);
    if (ret < 0) {
        pr_err("[FPGA_MEM] ERROR: Failed to add cdev: %d\n", ret);
        goto fail_cdev_add;
    }
    pr_info("[FPGA_MEM] Added cdev to system\n");

    /* Request I/O memory region */
    base_vir_mem_addr = request_mem_region(fpga_mem_offset_addr, FPGA_MEM_SIZE,
                                          "fpga_vir_mem");
    if (!base_vir_mem_addr) {
        pr_err("[FPGA_MEM] ERROR: request_mem_region failed!\n");
        pr_err("[FPGA_MEM]   Region 0x%08x-0x%08x may be in use\n",
               fpga_mem_offset_addr, fpga_mem_offset_addr + FPGA_MEM_SIZE - 1);
        ret = -EBUSY;
        goto fail_mem_region;
    }
    pr_info("[FPGA_MEM] Reserved I/O memory region 0x%08x-0x%08x\n",
            fpga_mem_offset_addr, fpga_mem_offset_addr + FPGA_MEM_SIZE - 1);

    /* Map physical memory to kernel virtual address space */
    base_vir_addr = ioremap(fpga_mem_offset_addr, FPGA_MEM_SIZE);
    if (!base_vir_addr) {
        pr_err("[FPGA_MEM] ERROR: ioremap failed!\n");
        ret = -ENOMEM;
        goto fail_ioremap;
    }
    pr_info("[FPGA_MEM] Mapped FPGA memory to kernel virtual address 0x%p\n",
            base_vir_addr);

    /* Create device class */
    fpga_mem_class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(fpga_mem_class)) {
        pr_err("[FPGA_MEM] ERROR: Failed to create device class\n");
        ret = PTR_ERR(fpga_mem_class);
        goto fail_class;
    }
    pr_info("[FPGA_MEM] Created device class '%s'\n", CLASS_NAME);

    /* Create device node /dev/fpga_mem */
    dev = device_create(fpga_mem_class, NULL, fpga_mem_num, NULL, DEVICE_NAME);
    if (IS_ERR(dev)) {
        pr_err("[FPGA_MEM] ERROR: Failed to create device\n");
        ret = PTR_ERR(dev);
        goto fail_device;
    }
    pr_info("[FPGA_MEM] Created device node /dev/%s\n", DEVICE_NAME);

    pr_info("[FPGA_MEM] ======================================\n");
    pr_info("[FPGA_MEM] Driver initialized successfully!\n");
    pr_info("[FPGA_MEM] Ready to serve mmap() requests\n");
    pr_info("[FPGA_MEM] ======================================\n");

    return 0;

fail_device:
    class_destroy(fpga_mem_class);
fail_class:
    iounmap(base_vir_addr);
fail_ioremap:
    release_mem_region(fpga_mem_offset_addr, FPGA_MEM_SIZE);
fail_mem_region:
    cdev_del(p_fpga_mem);
fail_cdev_add:
    kfree(p_fpga_mem);
fail_cdev_alloc:
    unregister_chrdev_region(fpga_mem_num, 1);
    return ret;
}

/* Module cleanup */
static void __exit fpga_mem_exit(void)
{
    pr_info("[FPGA_MEM] ======================================\n");
    pr_info("[FPGA_MEM] Removing driver\n");
    pr_info("[FPGA_MEM] Total mmap operations: %d\n",
            atomic_read(&mmap_count));

    device_destroy(fpga_mem_class, fpga_mem_num);
    pr_info("[FPGA_MEM] Destroyed device node\n");

    class_destroy(fpga_mem_class);
    pr_info("[FPGA_MEM] Destroyed device class\n");

    iounmap(base_vir_addr);
    pr_info("[FPGA_MEM] Unmapped kernel virtual address\n");

    release_mem_region(fpga_mem_offset_addr, FPGA_MEM_SIZE);
    pr_info("[FPGA_MEM] Released I/O memory region\n");

    cdev_del(p_fpga_mem);
    pr_info("[FPGA_MEM] Removed cdev\n");

    kfree(p_fpga_mem);
    unregister_chrdev_region(fpga_mem_num, 1);
    pr_info("[FPGA_MEM] Unregistered chrdev region\n");

    pr_info("[FPGA_MEM] Driver removed successfully\n");
    pr_info("[FPGA_MEM] ======================================\n");
}

module_init(fpga_mem_init);
module_exit(fpga_mem_exit);

MODULE_AUTHOR("HashSource (reimplemented from Bitmain)");
MODULE_DESCRIPTION("FPGA Memory Access Driver with Debug Logging");
MODULE_VERSION("1.0-debug");
