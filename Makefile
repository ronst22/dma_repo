TARGET= dma_spoof
OBJS  = dma-attack.o
CC=/opt/gcc-linaro-4.9-2015.05-x86_64_aarch64-linux-gnu/bin/aarch64-linux-gnu-gcc
LD=/opt/gcc-linaro-4.9-2015.05-x86_64_aarch64-linux-gnu/bin/aarch64-linux-gnu-ld
CURRENT=`uname -r`
KDIR = ../../kernel_obj/
PWD = $(shell pwd) 

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
