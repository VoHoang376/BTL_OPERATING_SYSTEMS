#include <stdio.h>
#include "common.h"
#include "syscall.h"

int __sys_xxxhandler(struct pcb_t *caller, struct sc_regs *regs) {
    (void)caller;
    /* Implements Section 1.6.3: Adding a system call */
    printf("The first system call parameter %d\n", regs ? (int)regs->a0 : 0);
    return 0;
}
