#include <linux/module.h>
#include <linux/delay.h>
#include <linux/fs.h>		/* for file_operations */
#include <linux/version.h>	/* versioning */
#include <linux/highmem.h>
#include <linux/slab.h>

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
	void *CONBLK_AD;	//Control Block Address
	uint32_t TI;		//transfer information; see DmaControlBlock.TI for description
	void *SOURCE_AD;	//Source address
	void *DEST_AD;		//Destination address
	uint32_t TXFR_LEN;	//transfer length.
	uint32_t STRIDE;	//2D Mode Stride. Only used if TI.TDMODE = 1
	void *NEXTCONBK;	//Next control block. Must be 256-bit aligned (32 bytes; 8 words)
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
	void *SOURCE_AD;	//Source address
	void *DEST_AD;		//Destination address
	uint32_t TXFR_LEN;	//transfer length.
	uint32_t STRIDE;	//2D Mode Stride. Only used if TI.TDMODE = 1
	void *NEXTCONBK;	//Next control block. Must be 256-bit aligned (32 bytes; 8 words)
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

static void dma_attack_cleanup(void)
{
	printk("dma_attack exit\n");
}

static long dma_start = 0x3f007000L;
//static int  dma_offset= ;
static int dma_attack_init(void)
{
	void *start;
	void *virtCbPage;
	void *physCbPage;
	char *virtSrcPage;
	void *physSrcPage;
	void *virtDstPage;
	void *physDstPage;
	struct DmaControlBlock *cb1;
	struct DmaChannelHeader *dmaHeader;

	int dmaChNum = 5;
	dmaHeader = kmalloc(4096, GFP_KERNEL);

	start = kmap((void *) &dma_start);
	virtCbPage = kmalloc(4096, GFP_KERNEL);
	physCbPage = (void *) virt_to_phys(virtCbPage);

	printk("dma_attack %lx:%p\n", dma_start, start);

	virtSrcPage = kmalloc(4096, GFP_KERNEL);
	physSrcPage = (void *) virt_to_phys(virtSrcPage);
	virtSrcPage[0] = 'h';
	virtSrcPage[1] = 'e';
	virtSrcPage[2] = 'l';
	virtSrcPage[3] = 'l';
	virtSrcPage[4] = 0;

	virtDstPage = kmalloc(4096, GFP_KERNEL);
	physDstPage = (void *) virt_to_phys(virtDstPage);

	//dedicate the first 8 words of this page to holding the cb.
	cb1 = (struct DmaControlBlock *) virtCbPage;

	//fill the control block:
	cb1->TI = DMA_CB_TI_SRC_INC | DMA_CB_TI_DEST_INC;	//after each byte copied, we want to increment the source and destination address of the copy, otherwise we'll be copying to the same address.
	cb1->SOURCE_AD = physSrcPage;	//set source and destination DMA address
	cb1->DEST_AD = physDstPage;
	cb1->TXFR_LEN = 12;	//transfer 12 bytes
	cb1->STRIDE = 0;	//no 2D stride
	cb1->NEXTCONBK = 0;	//no next control block


	printk("Before Dma: %s\n", (char *) virtDstPage);
	writeBitmasked(start + DMAENABLE / 4, 1 << dmaChNum,
		       1 << dmaChNum);
	dmaHeader =
	    (struct DmaChannelHeader *) (start + (DMACH(dmaChNum)) / 4);
	dmaHeader->CS = DMA_CS_RESET;	//make sure to disable dma first.
	mdelay(1000);		//give time for the reset command to be handled.
	dmaHeader->DEBUG = DMA_DEBUG_READ_ERROR | DMA_DEBUG_FIFO_ERROR | DMA_DEBUG_READ_LAST_NOT_SET_ERROR;	// clear debug error flags
	dmaHeader->CONBLK_AD = physCbPage;	//we have to point it to the PHYSICAL address of the control block (cb1)
	dmaHeader->CS = DMA_CS_ACTIVE;	//set active bit, but everything else is 0.
	mdelay(1000);

	printk("OUTPUT: %s\n", (char *) virtDstPage);
	kunmap(start);
	return -1;
}

module_init(dma_attack_init);
module_exit(dma_attack_cleanup);

MODULE_DESCRIPTION("memory tests");
MODULE_AUTHOR("Raz Ben Jehuda");
MODULE_LICENSE("GPL");
