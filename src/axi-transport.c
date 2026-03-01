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

static struct platform_device *virtio_mmio_pdev;
static struct resource virtio_mmio_resources[] = {
    {
        .start  = 0x1c000000,
        .end    = 0x1c000000 + 0x1000,
        .flags  = IORESOURCE_MEM,
    },
    // no interrupt
};
static void __iomem *base;

static int __init hello_init(void)
{
	printk(KERN_INFO "Hello, kernel world!\n");
	struct platform_device_info info = {
		.name         = "virtio-mmio",
		.id           = -1,
		.res          = virtio_mmio_resources,
		.num_res      = ARRAY_SIZE(virtio_mmio_resources),
		.dma_mask     = DMA_BIT_MASK(32),   // or 64
	};

	virtio_mmio_pdev = platform_device_register_full(&info);
	if (IS_ERR(virtio_mmio_pdev)) {
		pr_err("platform_device_register_full failed: %ld\n",
				PTR_ERR(virtio_mmio_pdev));
		return PTR_ERR(virtio_mmio_pdev);
	}

	// to view the output, use:
	//    xxd -s <phy addr> -l 128 /dev/shm/shm1
	// cma phy addr must be used (as offset), cuz RAM starts at 0x0
	// sprintf(virtio_regs_virt, "Hello world from direct CMA!\n");

	printk(KERN_INFO "Platform device registered\n");

	struct resource *res;

	res = request_mem_region(0x1c000000, 0x1000, "vcam");
	if (!res) {
		pr_err("cannot claim region 0x1c000000\n");
		return -EBUSY;
	}

	/* now safe to map */
	base = ioremap(0x1c000000ULL, 0x1000);
	if (!base) {
		release_mem_region(0x1c000000, 0x1000);
		return -ENOMEM;
	}

	// xxd -s 0x1c000000 -l 128 /dev/shm/shm1
        // 1c000000: 7669 7274 0200 0000 0c00 0000 feca edfe  virt............
        // 1c000010: 54ea 535c 2d5d 151c bd3e e3b7 45fc 6117  T.S\-]...>..E.a.
        // 1c000020: 8254 aba3 0e0f 52f2 9a40 d7a5 b402 bcee  .T....R..@......
        // 1c000030: e05c 7bdf d553 9f89 388b 47e8 547d e1a6  .\{..S..8.G.T}..
        // 1c000040: 855e 6a13 9ac9 75ab fc63 78b1 c283 18a9  .^j...u..cx.....
        // 1c000050: f4c4 eeab a871 4e72 560d b79c 9465 665d  .....qNrV....ef]
        // 1c000060: fdb5 6dea 5170 e1b0 a375 3934 6b0b c5fa  ..m.Qp...u94k...
        // 1c000070: 0100 0000 1586 5e9a b925 091e 7af5 64ae  ......^..%..z.d.

	iowrite32(0x74726976,  base + 0x00);   // MagicValue = "virt"
	iowrite32(2,           base + 0x04);   // Version = 2 (modern)
	iowrite32(12,          base + 0x08);   // DeviceID = example
	iowrite32(0xfeedcafe,  base + 0x0c);   // VendorID = arbitrary

	// must unmap, otherwise virtio-mmio won't able to claim
	iounmap(base);
	release_mem_region(0x1c000000, 0x1000);

	return 0;
}

static void __exit hello_exit(void)
{
	printk(KERN_INFO "Goodbye, cruel kernel world!\n");
}

module_init(hello_init);
module_exit(hello_exit);
