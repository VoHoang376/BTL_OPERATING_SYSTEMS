#include <stdio.h>
#include <stdlib.h>
#include "syscall.h"
#include "mm.h"

int __sys_listsyscall(struct pcb_t *caller, struct sc_regs *regs) {
    (void)caller;
    (void)regs;
    printf("=== Simple OS System Call Table ===\n");
    printf("  0: listsyscall - list all registered system calls\n");
    printf(" 17: memmap     - memory mapping operations\n");
    printf("440: xxx        - sample user-defined system call\n");
    return 0;
}

int __sys_memmap(struct pcb_t *caller, struct sc_regs *regs) {
    if (!caller || !regs) return -1;
    uint32_t op = regs->a0;

    switch (op) {
        case SYSMEM_MAP_OP:
            printf("[Syscall memmap] Operation SYSMEM_MAP_OP requested\n");
            break;
        case SYSMEM_INC_OP:
            printf("[Syscall memmap] Operation SYSMEM_INC_OP requested\n");
            inc_vma_limit(caller, regs->a1, regs->a2);
            break;
        case SYSMEM_SWP_OP:
            printf("[Syscall memmap] Operation SYSMEM_SWP_OP requested\n");
            __mm_swap_page(caller, regs->a1, regs->a2);
            break;
        case SYSMEM_IO_READ:
            printf("[Syscall memmap] Operation SYSMEM_IO_READ requested\n");
            break;
        case SYSMEM_IO_WRITE:
            printf("[Syscall memmap] Operation SYSMEM_IO_WRITE requested\n");
            break;
        default:
            printf("[Syscall memmap] Unknown sub-operation: %u\n", op);
            return -1;
    }
    return 0;
}

int syscall(int sys_num, struct pcb_t *caller, struct sc_regs *regs) {
    switch (sys_num) {
        case SYS_LISTSYSCALL:
            return __sys_listsyscall(caller, regs);
        case SYS_MEMMAP:
            return __sys_memmap(caller, regs);
        case SYS_XXX:
            return __sys_xxxhandler(caller, regs);
        default:
            printf("[Syscall] Error: Unknown system call number %d from PID %u\n", sys_num, caller ? caller->pid : 0);
            return -1;
    }
}
