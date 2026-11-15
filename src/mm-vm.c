#include <stdio.h>
#include <stdlib.h>
#include "mm.h"
#include "os-mm.h"

int inc_vma_limit(struct pcb_t *caller, int vmaid, int inc_sz) {
    if (!caller || !caller->mm) return -1;
    struct vm_area_struct *cur_vma = caller->mm->mmap;

    /* Nếu chưa có VMA thì khởi tạo vùng nhớ ảo mặc định (16MB) */
    if (!cur_vma) {
        cur_vma = (struct vm_area_struct *)malloc(sizeof(struct vm_area_struct));
        cur_vma->vm_id = vmaid;
        cur_vma->vm_start = 0;
        cur_vma->vm_end = 0x1000000; /* 16MB virtual limit */
        cur_vma->sbrk = 0;
        cur_vma->vm_mm = caller->mm;
        cur_vma->vm_freerg_list = NULL;
        cur_vma->vm_next = NULL;
        caller->mm->mmap = cur_vma;
    }

    while (cur_vma && (int)cur_vma->vm_id != vmaid) {
        cur_vma = cur_vma->vm_next;
    }
    if (!cur_vma) return -1;

    if (cur_vma->sbrk + inc_sz > cur_vma->vm_end) {
        printf("[VM] Error: Exceeded VM area limit [%lu, %lu]\n", cur_vma->vm_start, cur_vma->vm_end);
        return -1;
    }

    cur_vma->sbrk += inc_sz;
    return 0;
}

struct vm_rg_struct *get_free_vmrg_area(struct pcb_t *caller, int vmaid, int size) {
    if (!caller || !caller->mm) return NULL;
    struct vm_area_struct *cur_vma = caller->mm->mmap;
    while (cur_vma && (int)cur_vma->vm_id != vmaid) {
        cur_vma = cur_vma->vm_next;
    }
    if (!cur_vma) return NULL;

    /* Tìm trong danh sách vùng nhớ đã giải phóng (free regions list) */
    struct vm_rg_struct *prev = NULL;
    struct vm_rg_struct *curr = cur_vma->vm_freerg_list;
    while (curr) {
        if ((curr->rg_end - curr->rg_start) >= (unsigned long)size) {
            if (prev) prev->rg_next = curr->rg_next;
            else cur_vma->vm_freerg_list = curr->rg_next;
            curr->rg_next = NULL;
            return curr;
        }
        prev = curr;
        curr = curr->rg_next;
    }
    return NULL;
}
