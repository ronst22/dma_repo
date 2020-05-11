TARGET= dma_spoof
OBJS  = dma-attack.o
CC=/home/ron/projects/optee-project/toolchains/aarch64/bin/aarch64-linux-gnu-gcc


LD=/home/ron/projects/optee-project/toolchains/aarch64/bin/aarch64-linux-gnu-ld
CURRENT=`uname -r`
KDIR = /home/ron/projects/optee-project/linux/
PWD = $(shell pwd) 
CFLAGS_MODULE +=-mcmodel=large -mpc-relative-literal-loads
obj-m      := $(TARGET).o
$(TARGET)-objs   := $(OBJS)

default:
	make ARCH=arm64 -C $(KDIR) SUBDIRS=$(PWD) modules

$(TARGET).o: $(OBJS)
	$(LD) $(LD_RFLAG) -r -o $@ $(OBJS)
clean:
	-rm -f *.o .*.o.cmd .*.ko.cmd *.[ch]~ Modules.symvers *.order *.*.ko *.ko *.mod.c .*.c  Module.symvers
indent:
	indent -kr -i8 *.[ch]
