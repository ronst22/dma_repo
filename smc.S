#include <linux/init.h>
#include <linux/linkage.h>

#include <asm/assembler.h>

.syntax unified
.arch_extension virt
.arch_extension sec

.text

ENTRY(smc_call)
        smc  #0
        mov pc,lr
ENDPROC(smc_call)

