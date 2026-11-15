#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mm64.h"

/* Canonical address check: bits 63:57 must all be 0s or all 1s */
int mm64_is_canonical(uint64_t vaddr) {
    uint64_t top_bits = (vaddr >> 57) & 0x7F;
    return (top_bits == 0x00 || top_bits == 0x7F);
}

int mm64_init_pagetable(struct mm_struct *mm) {
    if (!mm) return -1;
    /* Allocate Level 5 Page Global Directory (PGD) */
    mm->pgd = (uint64_t *)calloc(PAGING64_ENTRY_COUNT, sizeof(uint64_t));
    if (!mm->pgd) return -1;
    return 0;
}

int mm64_map_page(struct mm_struct *mm, uint64_t vaddr, uint32_t fpn, uint32_t flags) {
    if (!mm || !mm->pgd) return -1;
    if (!mm64_is_canonical(vaddr)) {
        fprintf(stderr, "[MM64] Error: Non-canonical address 0x%016llx\n", (unsigned long long)vaddr);
        return -1;
    }

    uint64_t *curr_tbl = mm->pgd;

    /* L5 (PGD) -> L4 (P4D) */
    uint64_t l5_idx = pgd_index(vaddr);
    if (!curr_tbl[l5_idx]) {
        curr_tbl[l5_idx] = (uint64_t)(uintptr_t)calloc(PAGING64_ENTRY_COUNT, sizeof(uint64_t));
    }
    curr_tbl = (uint64_t *)(uintptr_t)curr_tbl[l5_idx];

    /* L4 (P4D) -> L3 (PUD) */
    uint64_t l4_idx = p4d_index(vaddr);
    if (!curr_tbl[l4_idx]) {
        curr_tbl[l4_idx] = (uint64_t)(uintptr_t)calloc(PAGING64_ENTRY_COUNT, sizeof(uint64_t));
    }
    curr_tbl = (uint64_t *)(uintptr_t)curr_tbl[l4_idx];

    /* L3 (PUD) -> L2 (PMD) */
    uint64_t l3_idx = pud_index(vaddr);
    if (!curr_tbl[l3_idx]) {
        curr_tbl[l3_idx] = (uint64_t)(uintptr_t)calloc(PAGING64_ENTRY_COUNT, sizeof(uint64_t));
    }
    curr_tbl = (uint64_t *)(uintptr_t)curr_tbl[l3_idx];

    /* L2 (PMD) -> L1 (PT) */
    uint64_t l2_idx = pmd_index(vaddr);
    if (!curr_tbl[l2_idx]) {
        curr_tbl[l2_idx] = (uint64_t)(uintptr_t)calloc(PAGING64_ENTRY_COUNT, sizeof(uint64_t));
    }
    curr_tbl = (uint64_t *)(uintptr_t)curr_tbl[l2_idx];

    /* L1 (PT) -> PTE Entry */
    uint64_t l1_idx = pt_index(vaddr);
    curr_tbl[l1_idx] = ((uint64_t)fpn & PTE_FPN_MASK) | PTE_PRESENT_BIT | flags;

    printf("[MM64] Mapped VA 0x%016llx -> L5:%llu L4:%llu L3:%llu L2:%llu L1:%llu to FPN %u\n",
           (unsigned long long)vaddr, 
           (unsigned long long)l5_idx, (unsigned long long)l4_idx, 
           (unsigned long long)l3_idx, (unsigned long long)l2_idx, 
           (unsigned long long)l1_idx, fpn);

    return 0;
}

int mm64_translate_address(struct mm_struct *mm, uint64_t vaddr, uint64_t *paddr) {
    if (!mm || !mm->pgd || !paddr) return -1;
    if (!mm64_is_canonical(vaddr)) return -1;

    uint64_t *curr_tbl = mm->pgd;

    /* L5 -> L4 */
    uint64_t l5_idx = pgd_index(vaddr);
    if (!curr_tbl[l5_idx]) return -1;
    curr_tbl = (uint64_t *)(uintptr_t)curr_tbl[l5_idx];

    /* L4 -> L3 */
    uint64_t l4_idx = p4d_index(vaddr);
    if (!curr_tbl[l4_idx]) return -1;
    curr_tbl = (uint64_t *)(uintptr_t)curr_tbl[l4_idx];

    /* L3 -> L2 */
    uint64_t l3_idx = pud_index(vaddr);
    if (!curr_tbl[l3_idx]) return -1;
    curr_tbl = (uint64_t *)(uintptr_t)curr_tbl[l3_idx];

    /* L2 -> L1 */
    uint64_t l2_idx = pmd_index(vaddr);
    if (!curr_tbl[l2_idx]) return -1;
    curr_tbl = (uint64_t *)(uintptr_t)curr_tbl[l2_idx];

    /* L1 Entry */
    uint64_t l1_idx = pt_index(vaddr);
    uint64_t pte = curr_tbl[l1_idx];
    if (!(pte & PTE_PRESENT_BIT)) {
        return -1; /* Page fault / Page not present */
    }

    uint32_t fpn = (uint32_t)(pte & PTE_FPN_MASK);
    *paddr = ((uint64_t)fpn * PAGING_PAGESZ) + page_offset(vaddr);
    return 0;
}
