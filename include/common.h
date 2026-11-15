#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include "os-cfg.h"

typedef uint32_t addr_t;
typedef uint8_t BYTE;

/* Instruction opcodes recognized by the CPU/simulation */
enum ins_opcode_t {
    CALC = 0,
    ALLOC,
    KMALLOC,
    FREE,
    READ,
    WRITE,
    COPY_FROM_USER,
    COPY_TO_USER,
    DMA_MEMCPY,
    SYSCALL
};

/* Structure representing a single program instruction */
struct instruction_t {
    enum ins_opcode_t opcode;
    uint64_t arg0;
    uint64_t arg1;
    uint64_t arg2;
    uint64_t arg3;
};

/* Code segment holding instructions */
struct code_seg_t {
    int size;
    struct instruction_t *text;
};

/* Forward declarations */
struct queue_t;
struct mm_struct;
struct memphy_struct;
struct page_table_t;

/* Process Control Block (PCB) */
struct pcb_t {
    uint32_t pid;
    uint32_t priority;            /* Default priority */
    char path[100];
    struct code_seg_t *code;      /* Instruction stream */
    addr_t regs[10];              /* 10 general purpose registers: 0..9 */
    uint32_t pc;                  /* Program counter */
#ifdef MLQ_SCHED
    uint32_t prio;                /* Runtime priority (overwrites default if given) */
#endif
    struct page_table_t *page_table; /* Obsolete legacy table */
    uint32_t bp;                  /* Break pointer for heap management */
#ifdef MM_PAGING
    struct mm_struct *mm;
    struct memphy_struct *mram;
    struct memphy_struct **mswp;
    struct memphy_struct *active_mswp;
#endif
};

/* Kernel global management structure */
struct krnl_t {
    struct queue_t *ready_queue;
    struct queue_t *running_list;
#ifdef MLQ_SCHED
    struct queue_t *mlq_ready_queue; /* Array of ready queues for each priority */
#endif
#ifdef MM_PAGING
    struct mm_struct *mm;
    struct memphy_struct *mram;
    struct memphy_struct **mswp;
    struct memphy_struct *active_mswp;
    uint32_t active_mswp_id;
#endif
    pthread_mutex_t kernel_lock;     /* Synchronization lock */
};

extern struct krnl_t kernel;

#endif /* COMMON_H */
