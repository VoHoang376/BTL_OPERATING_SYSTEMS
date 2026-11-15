#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>
#include "common.h"

/* System call register structure */
struct sc_regs {
    uint32_t a0;
    uint32_t a1;
    uint32_t a2;
    uint32_t a3;
};

/* System call numbers */
#define SYS_LISTSYSCALL 0
#define SYS_MEMMAP      17
#define SYS_XXX         440

/* Sub-operations for memmap syscall */
#define SYSMEM_MAP_OP   1
#define SYSMEM_INC_OP   2
#define SYSMEM_SWP_OP   3
#define SYSMEM_IO_READ  4
#define SYSMEM_IO_WRITE 5

/* System call entry point and handlers */
int syscall(int sys_num, struct pcb_t *caller, struct sc_regs *regs);
int __sys_listsyscall(struct pcb_t *caller, struct sc_regs *regs);
int __sys_memmap(struct pcb_t *caller, struct sc_regs *regs);
int __sys_xxxhandler(struct pcb_t *caller, struct sc_regs *regs);

#endif /* SYSCALL_H */
