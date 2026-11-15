#ifndef MM_H
#define MM_H

#include "common.h"
#include "os-mm.h"

/* Memory management API */
int init_mm(struct mm_struct *mm, struct pcb_t *caller);
int vmap_page_range(struct pcb_t *caller, int addr, int pgnum, 
                    struct framephy_struct *frames, struct vm_rg_struct *ret_rg);
int alloc_pages_range(struct pcb_t *caller, int req_pgnum, struct framephy_struct **frm_lst);
int free_pcb_memph(struct pcb_t *caller);
int inc_vma_limit(struct pcb_t *caller, int vmaid, int inc_sz);
int __mm_swap_page(struct pcb_t *caller, int vicpgn, int swppgn);

/* Page table entry bitfield manipulation helpers */
int pte_set_fpn(uint32_t *pte, int fpn);
int pte_set_swap(uint32_t *pte, int swptyp, int swpoff);

/* Physical memory (MEMPHY) hardware operations */
int init_memphy(struct memphy_struct *mp, int max_size, int random_flg);
int MEMPHY_read(struct memphy_struct *mp, int addr, BYTE *value);
int MEMPHY_write(struct memphy_struct *mp, int addr, BYTE data);
int MEMPHY_format(struct memphy_struct *mp, int pagesz);
int MEMPHY_get_freefp(struct memphy_struct *mp, int *retfpn);
int MEMPHY_put_freefp(struct memphy_struct *mp, int fpn);

#endif /* MM_H */
