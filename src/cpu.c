#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include "cpu.h"
#include "sched.h"
#include "dma.h"
#include "syscall.h"
#include "libmem.h"
#include "libstd.h"

void check_and_handle_interrupts(struct cpu_t *cpu) {
    /* TV4 - Check and service hardware interrupt from DMA co-processor */
    if (dma_check_irq()) {
        dma_isr(cpu->id);
    }
}

int run(struct pcb_t *proc) {
    if (!proc || !proc->code) return -1;
    if (proc->pc >= (uint32_t)proc->code->size) return 0;

    struct instruction_t *ins = &proc->code->text[proc->pc];
    printf("[CPU] PID %u executing instruction #%u (Opcode %d)\n", proc->pid, proc->pc, ins->opcode);

    switch (ins->opcode) {
        case CALC:
            /* Calculation takes 1 cycle */
            break;
        case ALLOC:
            libmem_alloc(proc, (uint32_t)ins->arg0, (uint32_t)ins->arg1);
            break;
        case KMALLOC:
            /* Kernel continuous physical allocation */
            printf("[CPU] KMALLOC %lu bytes into reg %lu (Kernel Space)\n", 
                   (unsigned long)ins->arg0, (unsigned long)ins->arg1);
            proc->regs[ins->arg1] = 0x80000000; /* Simulated Kernel space pointer */
            break;
        case FREE:
            libmem_free(proc, (uint32_t)ins->arg0);
            break;
        case READ:
            libmem_read(proc, (uint32_t)ins->arg0, (uint32_t)ins->arg1, (uint32_t)ins->arg2);
            break;
        case WRITE:
            libmem_write(proc, (uint32_t)ins->arg0, (uint32_t)ins->arg1, (uint32_t)ins->arg2);
            break;
        case COPY_FROM_USER: {
            uint8_t kbuf[256] = {0};
            uint32_t sz = ins->arg3 > 256 ? 256 : (uint32_t)ins->arg3;
            copy_from_user(proc->pid, kbuf, (addr_t)(ins->arg0 + ins->arg2), sz);
            break;
        }
        case COPY_TO_USER: {
            uint8_t kbuf[256] = {0xAA, 0xBB, 0xCC, 0xDD};
            uint32_t sz = ins->arg3 > 256 ? 256 : (uint32_t)ins->arg3;
            copy_to_user(proc->pid, (addr_t)(ins->arg1 + ins->arg2), kbuf, sz);
            break;
        }
        case DMA_MEMCPY: {
            /* TV3 & TV4: Co-processor DMA memory transfer integration */
            uint64_t src_base = ins->arg0;
            uint64_t dst_base = ins->arg1;

            /* Nếu tham số chỉ định là thanh ghi (0..9) và thanh ghi có giá trị */
            if (src_base < 10 && proc->regs[src_base] != 0) {
                src_base = proc->regs[src_base];
            }
            if (dst_base < 10 && proc->regs[dst_base] != 0) {
                dst_base = proc->regs[dst_base];
            }

            uint64_t src_addr = src_base + ins->arg2; /* + offset */
            uint64_t dst_addr = dst_base + ins->arg2; /* + offset */
            uint32_t size = (uint32_t)ins->arg3;

            printf("[CPU %u] DMA_MEMCPY instruction: src=0x%llx, dst=0x%llx, size=%u\n",
                   proc->pid, (unsigned long long)src_addr, (unsigned long long)dst_addr, size);

            /* Khởi động DMA truyền bất đồng bộ (Non-blocking): CPU không cần copy từng byte */
            dma_trigger_async(src_addr, dst_addr, size);
            break;
        }
        case SYSCALL: {
            struct sc_regs regs = { (uint32_t)ins->arg1, 0, 0, 0 };
            printf("[CPU] SYSCALL %lu\n", (unsigned long)ins->arg0);
            syscall((int)ins->arg0, proc, &regs);
            break;
        }
        default:
            break;
    }

    proc->pc++;
    return (proc->pc < (uint32_t)proc->code->size) ? 1 : 0;
}

void *cpu_routine(void *arg) {
    struct cpu_t *cpu = (struct cpu_t *)arg;
    while (cpu->running) {
        check_and_handle_interrupts(cpu);

        struct pcb_t *proc = get_proc();
        if (proc == NULL) {
            usleep(50000); /* 50ms idle if no processes */
            continue;
        }

        cpu->current_proc = proc;
        int remaining_slice = cpu->time_slice;
        while (remaining_slice > 0) {
            check_and_handle_interrupts(cpu);
            int status = run(proc);
            remaining_slice--;
            if (status == 0) {
                /* Process has completed all instructions */
                printf("[CPU %d] PID %u finished execution.\n", cpu->id, proc->pid);
                break;
            }
        }
        check_and_handle_interrupts(cpu);

        if (proc->pc < (uint32_t)proc->code->size) {
            /* Still has instructions left: requeue back to scheduler */
            put_proc(proc);
        } else {
            /* TODO: Cleanup finished process resources */
        }
        cpu->current_proc = NULL;
    }
    return NULL;
}
