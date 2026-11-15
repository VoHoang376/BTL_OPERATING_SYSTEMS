#ifndef MM64_H
#define MM64_H

#include <stdint.h>
#include "os-mm.h"
#include "common.h"

/*
 * 64-bit 5-Level Paging Architecture:
 * - Bits 63-57: Canonical address check
 * - Bits 56-48: Level 5 - Page Global Directory (PGD) [9 bits, 512 entries]
 * - Bits 47-39: Level 4 - Page Level 4 Directory (P4D) [9 bits, 512 entries]
 * - Bits 38-30: Level 3 - Page Upper Directory (PUD)   [9 bits, 512 entries]
 * - Bits 29-21: Level 2 - Page Middle Directory (PMD)  [9 bits, 512 entries]
 * - Bits 20-12: Level 1 - Page Table (PT)              [9 bits, 512 entries]
 * - Bits 11-0:  Page Offset                            [12 bits, 4KB page]
 */

#define PAGING64_LEVELS 5
#define PAGING64_ENTRY_COUNT 512
#define PAGING64_OFFSET_MASK 0xFFFULL
#define PAGING64_INDEX_MASK  0x1FFULL

#define pgd_index(vaddr) (((vaddr) >> 48) & PAGING64_INDEX_MASK)
#define p4d_index(vaddr) (((vaddr) >> 39) & PAGING64_INDEX_MASK)
#define pud_index(vaddr) (((vaddr) >> 30) & PAGING64_INDEX_MASK)
#define pmd_index(vaddr) (((vaddr) >> 21) & PAGING64_INDEX_MASK)
#define pt_index(vaddr)  (((vaddr) >> 12) & PAGING64_INDEX_MASK)
#define page_offset(vaddr) ((vaddr) & PAGING64_OFFSET_MASK)

/* Page Table Entry (PTE) Flags & Masks (Figure 6) */
#define PTE_PRESENT_BIT   (1U << 31)
#define PTE_SWAPPED_BIT   (1U << 30)
#define PTE_RESERVED_BIT  (1U << 29)
#define PTE_DIRTY_BIT     (1U << 28)
#define PTE_FPN_MASK      (0x1FFF)     /* Bits 0-12 */
#define PTE_SWP_TYPE_MASK (0x1F)       /* Bits 0-4 */
#define PTE_SWP_OFF_SHIFT 5
#define PTE_SWP_OFF_MASK  (0x1FFFFF)   /* Bits 5-25 */

/* 64-bit Paging Functions */
int mm64_init_pagetable(struct mm_struct *mm);
int mm64_map_page(struct mm_struct *mm, uint64_t vaddr, uint32_t fpn, uint32_t flags);
int mm64_translate_address(struct mm_struct *mm, uint64_t vaddr, uint64_t *paddr);
int mm64_is_canonical(uint64_t vaddr);

#endif /* MM64_H */
