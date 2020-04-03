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

static void invalid_reader_cleanup(void)
{
	printk("dma_attack exit\n");
}

static int addr = 0;
module_param(addr,int,0660);

static int __init invalid_reader_init(void)
{
	volatile void *start;
	struct file* F;

	char *virtDstPage;
	void *physDstPage;
	void* physSrcPage;
	volatile struct DmaChannelHeader *dmaHeader;


	// virtDstPage = (char*)__get_dma_pages(GFP_ATOMIC, 0);
	
	// if (virtDstPage == NULL) {
	// 	pr_err("Failed allocated virtDstPage\n");
	// 	return -1;
	// }

	// memset(virtDstPage, 0, 4096);

	// physDstPage = (void *) virt_to_phys(virtDstPage);

	virtDstPage = kmap(addr);

	F = file_open("/home/Page.blob",  O_CREAT |  O_RDWR | O_APPEND, S_IRWXU | S_IRWXG | S_IRWXO);
	file_write(F, 0, (char*)virtDstPage, 4096);
	file_sync(F); 
	file_close(F);

	return 0;
}	

module_init(invalid_reader_init);
module_exit(invalid_reader_cleanup);

MODULE_DESCRIPTION("memory tests");
MODULE_AUTHOR("Ron Stajnrod");
MODULE_LICENSE("GPL");
