/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Caitoa release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

//#ifdef MM_PAGING
/*
 * PAGING based Memory Management
 * Virtual memory module mm/mm-vm.c
 */

#include "string.h"
#include "mm64.h"
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

/*get_vma_by_num - get vm area by numID
 *@mm: memory region
 *@vmaid: ID vm area to alloc memory region
 *
 */
struct vm_area_struct *get_vma_by_num(struct mm_struct *mm, int vmaid)
{
  if (mm == NULL || vmaid < 0)
    return NULL;

  for (struct vm_area_struct *vma = mm->mmap; vma != NULL; vma = vma->vm_next)
    if (vma->vm_id == (unsigned long)vmaid)
      return vma;

  return NULL;
}

int __mm_swap_page(struct pcb_t *caller, addr_t vicfpn , addr_t swpfpn)
{
    __swap_cp_page(caller->krnl->mram, vicfpn, caller->krnl->active_mswp, swpfpn);
    return 0;
}

/*get_vm_area_node - get vm area for a number of pages
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@incpgnum: number of page
 *@vmastart: vma end
 *@vmaend: vma end
 *
 */
struct vm_rg_struct *get_vm_area_node_at_brk(struct pcb_t *caller, int vmaid, addr_t alignedsz)
{
  struct vm_rg_struct * newrg;
  /* TODO retrive current vma to obtain newrg, current comment out due to compiler redundant warning*/
  //struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);

  //newrg = malloc(sizeof(struct vm_rg_struct));

  /* TODO: update the newrg boundary
  // newrg->rg_start = ...
  // newrg->rg_end = ...
  */
  // struct vm_area_struct *cur_vma=....

  /* END TODO */

  return newrg;
}

/*validate_overlap_vm_area
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@vmastart: vma end
 *@vmaend: vma end
 *
 */
int validate_overlap_vm_area(struct pcb_t *caller, int vmaid,
                             addr_t vmastart, addr_t vmaend)
{
  if (caller == NULL || vmastart >= vmaend)
    return -1;

  struct vm_area_struct *cur_area = get_vma_by_num(caller->mm, vmaid);
  if (cur_area == NULL)
    return -1;

  for (struct vm_area_struct *vma = caller->mm->mmap; vma != NULL; vma = vma->vm_next)
    if (vma != cur_area && OVERLAP(vmastart, vmaend, vma->vm_start, vma->vm_end))
      return -1;

  return 0;
}

/*inc_vma_limit - increase vm area limits to reserve space for new variable
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@inc_sz: increment size
 *
 */
int inc_vma_limit(struct pcb_t *caller, int vmaid, addr_t inc_sz)
{
  if (caller == NULL || inc_sz == 0)
    return -1;

  struct vm_area_struct *vma = get_vma_by_num(caller->mm, vmaid);
  if (vma == NULL)
    return -1;

  addr_t pages = inc_sz / PAGING64_PAGESZ + (inc_sz % PAGING64_PAGESZ != 0);
  addr_t old_end = vma->vm_end;
  if (pages > (addr_t)INT32_MAX || pages > (UINT64_MAX - old_end) / PAGING64_PAGESZ)
    return -1;

  addr_t new_end = old_end + pages * PAGING64_PAGESZ;
  if (validate_overlap_vm_area(caller, vmaid, old_end, new_end) != 0)
    return -1;

  struct vm_rg_struct *region = init_vm_rg(old_end, new_end);
  if (region == NULL)
    return -1;

  if (vm_map_ram(caller, vma->vm_start, new_end, old_end, (int)pages, region) != 0)
  {
    free(region);
    return -1;
  }

  region->vmaid = vmaid;
  enlist_vm_rg_node(&vma->vm_freerg_list, region);
  vma->vm_end = new_end;

  return 0;
}

// #endif
