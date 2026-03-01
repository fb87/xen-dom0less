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

static void *cpu_virt_addr;
static dma_addr_t dma_handle;
static const size_t ALLOC_SIZE = 2 << 20;  // 2 MiB — should come from CMA

static struct platform_device v2m_virtio_device = {
	.name = "virtio-mmio",
	.id = -1,
	.num_resources = 2,
	.resource = (struct resource []) {
		{
			.start = 0x1001e000,
			.end = 0x1001e0ff,
			.flags = IORESOURCE_MEM,
		}, {
			.start = 42 + 32,
			.end = 42 + 32,
			.flags = IORESOURCE_IRQ,
		},
	}
};

static int __init hello_init(void)
{
	printk(KERN_INFO "Hello, kernel world!\n");
	platform_device_register(&v2m_virtio_device);


	cpu_virt_addr = dma_alloc_coherent(&v2m_virtio_device.dev, ALLOC_SIZE,
			&dma_handle, GFP_KERNEL);
	if (!cpu_virt_addr) {
		pr_err("dma_alloc_coherent failed for %zu bytes\n", ALLOC_SIZE);
		return -ENOMEM;
	}

	pr_info("CMA allocation OK!\n");
	pr_info("  Virtual addr  : 0x%px\n", cpu_virt_addr);
	pr_info("  Physical addr : 0x%px\n", virt_to_phys(cpu_virt_addr));
	pr_info("  DMA addr      : %pad\n", &dma_handle);
	pr_info("  Size          : %zu bytes (%zu MiB)\n", ALLOC_SIZE,
			ALLOC_SIZE >> 20);

	// to view the output, use:
	//    xxd -s <phy addr> -l 128 /dev/shm/shm1
	// cma phy addr must be used (as offset), cuz RAM starts at 0x0
	sprintf(cpu_virt_addr, "Hello world from direct CMA!\n");

	printk(KERN_INFO "Platform device registered\n");

	return 0;
}

static void __exit hello_exit(void)
{
	printk(KERN_INFO "Goodbye, cruel kernel world!\n");
}

module_init(hello_init);
module_exit(hello_exit);
