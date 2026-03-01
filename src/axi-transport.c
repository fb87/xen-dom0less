// vim: tabstop=8 shiftwidth=8 autoindent colorcolumn=80

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <linux/platform_device.h>
#include <linux/virtio.h>
#include <linux/virtio_config.h>
#include <linux/virtio_ring.h>

#include <linux/dma-mapping.h>
#include <linux/slab.h>

MODULE_LICENSE("GPL");

MODULE_AUTHOR("S Dao <dsi1hc@bosch.com>");
MODULE_DESCRIPTION("The smallest possible hello world kernel module");

static void     *virtio_regs_virt;
static dma_addr_t virtio_regs_dma;
static const size_t ALLOC_SIZE = 2 << 20;  // 2 MiB — should come from CMA

static struct platform_device *virtio_mmio_pdev;
static struct resource virtio_mmio_resources[1];

static int __init hello_init(void)
{
	printk(KERN_INFO "Hello, kernel world!\n");
	struct platform_device_info info = {
		.name         = "virtio-mmio",
		.id           = -1,
		.res          = virtio_mmio_resources,
		.num_res      = 1,
		.dma_mask     = DMA_BIT_MASK(32),   // or 64
	};

	virtio_mmio_pdev = platform_device_register_full(&info);
	if (IS_ERR(virtio_mmio_pdev)) {
		pr_err("platform_device_register_full failed: %ld\n",
				PTR_ERR(virtio_mmio_pdev));
		return PTR_ERR(virtio_mmio_pdev);
	}

	virtio_regs_virt = dma_alloc_coherent(&virtio_mmio_pdev->dev,
			ALLOC_SIZE, &virtio_regs_dma, GFP_KERNEL);
	if (!virtio_regs_virt) {
		pr_err("dma_alloc_coherent failed for %zu bytes\n",
				ALLOC_SIZE);
		return -ENOMEM;
	}

	pr_info("CMA allocation OK!\n");
	pr_info("  Virtual addr  : 0x%px\n", virtio_regs_virt);
	pr_info("  Physical addr : 0x%px\n", virt_to_phys(virtio_regs_virt));
	pr_info("  DMA addr      : %pad\n", &virtio_regs_dma);
	pr_info("  Size          : %zu bytes (%zu MiB)\n", ALLOC_SIZE,
			ALLOC_SIZE >> 20);

	// to view the output, use:
	//    xxd -s <phy addr> -l 128 /dev/shm/shm1
	// cma phy addr must be used (as offset), cuz RAM starts at 0x0
	// sprintf(virtio_regs_virt, "Hello world from direct CMA!\n");

	printk(KERN_INFO "Platform device registered\n");

	iowrite32(0x74726976,  virtio_regs_virt + 0x00);   // MagicValue = "virt"
	iowrite32(2,           virtio_regs_virt + 0x04);   // Version = 2 (modern)
	iowrite32(18,          virtio_regs_virt + 0x08);   // DeviceID = example: 18 = virtio-rng
	iowrite32(0xfeedcafe,  virtio_regs_virt + 0x0c);   // VendorID = arbitrary

	iowrite32(1 << 1,      virtio_regs_virt + 0x10);     // VIRTIO_F_VERSION_1

	// Optional: some features
	iowrite32(1 << 0,      virtio_regs_virt + 0x10);   // VIRTIO_F_ANY_LAYOUT or similar

	pr_info("Fake virtio-mmio registers written at virtual %px (phys 0x%llx)\n",
			virtio_regs_virt, (u64)virtio_regs_dma);

	virtio_mmio_resources[0].start = virt_to_phys(virtio_regs_virt);
	virtio_mmio_resources[0].end   = virt_to_phys(virtio_regs_virt) + ALLOC_SIZE;
	virtio_mmio_resources[0].flags = IORESOURCE_MEM;

	pr_info("Dynamic virtio-mmio registered using CMA phys 0x%llx\n",
			(u64)virt_to_phys(virtio_regs_virt));

	return 0;
}

static void __exit hello_exit(void)
{
	printk(KERN_INFO "Goodbye, cruel kernel world!\n");

	dma_free_coherent(&virtio_mmio_pdev->dev, ALLOC_SIZE,
                      virtio_regs_virt, virtio_regs_dma);
}

module_init(hello_init);
module_exit(hello_exit);
