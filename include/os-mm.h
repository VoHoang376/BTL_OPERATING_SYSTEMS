#ifndef OS_MM_H
#define OS_MM_H

#include <stdint.h>
#include <pthread.h>
#include "os-cfg.h"

/* Privilege mode bits for memory regions */
#define VM_MODE_KERNEL 0
#define VM_MODE_USER   1

/* Memory region struct */
struct vm_rg_struct {
    unsigned long rg_start;
    unsigned long rg_end;
    /* Privilege mode bit: usermode = 1, kernelmode = 0 */
    unsigned long mode_bit;
    struct vm_rg_struct *rg_next;
};

/* Forward declaration */
struct mm_struct;

/* Memory area struct */
struct vm_area_struct {
    unsigned long vm_id;
    unsigned long vm_start;
    unsigned long vm_end;
    unsigned long sbrk;
    /* Derived: vm_limit = vm_end - vm_start */
    struct mm_struct *vm_mm;
    struct vm_rg_struct *vm_freerg_list;
    struct vm_area_struct *vm_next;
};

/* Page Number tracking list */
struct pgn_t {
    int pgn;
    struct pgn_t *pg_next;
};

/* Kmem cache pool (optional placeholder) */
struct kcache_pool_struct {
    int pool_id;
    void *pool_mem;
};

/* Memory mapping struct */
struct mm_struct {
#ifdef MM64
    uint64_t *pgd;
    uint64_t *p4d;
    uint64_t *pud;
    uint64_t *pmd;
    uint64_t *pt;
#else
    uint32_t *pgd;
#endif
    struct vm_area_struct *mmap;
    /* Currently we support a fixed number of symbols */
    struct vm_rg_struct symrgtbl[PAGING_MAX_SYMTBL_SZ];
    struct pgn_t *fifo_pgn;
    /* kmem cache pool */
    struct kcache_pool_struct *kcpooltbl;
};

/* FRAME/MEM PHY struct */
struct framephy_struct {
    int fpn;
    struct framephy_struct *fp_next;
};

/* Physical memory struct (RAM & SWAP) */
struct memphy_struct {
    /* Basic field of data and size */
    uint8_t *storage;
    int maxsz;
    /* Sequential device fields */
    int rdmflg;
    int cursor;
    /* Management structure */
    struct framephy_struct *free_fp_list;
    struct framephy_struct *used_fp_list;
    pthread_mutex_t lock;
};

#endif /* OS_MM_H */
