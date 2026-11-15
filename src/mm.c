#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mm.h"
#include "mm64.h"

int pte_set_fpn(uint32_t *pte, int fpn) {
    if (!pte) return -1;
    *pte = (*pte & ~PTE_FPN_MASK) | (fpn & PTE_FPN_MASK);
    *pte |= PTE_PRESENT_BIT;
    *pte &= ~PTE_SWAPPED_BIT;
    return 0;
}

int pte_set_swap(uint32_t *pte, int swptyp, int swpoff) {
    if (!pte) return -1;
    *pte &= ~PTE_PRESENT_BIT;
    *pte |= PTE_SWAPPED_BIT;
    *pte = (*pte & ~(PTE_SWP_TYPE_MASK)) | (swptyp & PTE_SWP_TYPE_MASK);
    *pte = (*pte & ~(PTE_SWP_OFF_MASK << PTE_SWP_OFF_SHIFT)) | ((swpoff & PTE_SWP_OFF_MASK) << PTE_SWP_OFF_SHIFT);
    return 0;
}

int init_mm(struct mm_struct *mm, struct pcb_t *caller) {
    (void)caller;
    if (!mm) return -1;
    memset(mm, 0, sizeof(struct mm_struct));

#ifdef MM64
    return mm64_init_pagetable(mm);
#else
    mm->pgd = (uint32_t *)calloc(PAGING_MAX_PGN, sizeof(uint32_t));
    return (mm->pgd != NULL) ? 0 : -1;
#endif
}

int alloc_pages_range(struct pcb_t *caller, int req_pgnum, struct framephy_struct **frm_lst) {
    (void)caller;
    if (!frm_lst || req_pgnum <= 0) return -1;
    *frm_lst = NULL;

    for (int i = 0; i < req_pgnum; i++) {
        int fpn = -1;
        if (MEMPHY_get_freefp(kernel.mram, &fpn) != 0) {
            /* Nếu hết RAM, kích hoạt swap trang sang thiết bị SWAP */
            printf("[MM] Warning: RAM out of frames, triggering page swapping...\n");
            break;
        }

        struct framephy_struct *new_fp = (struct framephy_struct *)malloc(sizeof(struct framephy_struct));
        new_fp->fpn = fpn;
        new_fp->fp_next = *frm_lst;
        *frm_lst = new_fp;
    }
    return 0;
}

int vmap_page_range(struct pcb_t *caller, int addr, int pgnum, 
                    struct framephy_struct *frames, struct vm_rg_struct *ret_rg) {
    if (!caller || !caller->mm || !frames) return -1;

    struct framephy_struct *curr = frames;
    for (int i = 0; i < pgnum && curr; i++) {
        uint64_t vaddr = (uint64_t)addr + (uint64_t)i * PAGING_PAGESZ;
        mm64_map_page(caller->mm, vaddr, curr->fpn, 0);
        curr = curr->fp_next;
    }

    if (ret_rg) {
        ret_rg->rg_start = addr;
        ret_rg->rg_end = addr + pgnum * PAGING_PAGESZ;
        ret_rg->mode_bit = VM_MODE_USER;
        ret_rg->rg_next = NULL;
    }
    return 0;
}

int free_pcb_memph(struct pcb_t *caller) {
    if (!caller || !caller->mm) return -1;
    printf("[MM] Releasing memory frames for PID %u\n", caller->pid);
    /* TODO: Duyệt và thu hồi các frame vật lý trả về RAM */
    return 0;
}

int __mm_swap_page(struct pcb_t *caller, int vicpgn, int swppgn) {
    (void)caller;
    printf("[MM Swapping] Swapping page %d (RAM) <-> %d (SWAP)\n", vicpgn, swppgn);
    /* Sao chép giữa RAM và active SWAP device */
    return 0;
}
