/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* LamiaAtrium release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

/*
 * PAGING based Memory Management
 * Memory management unit mm/mm.c
 */
#include "mm64.h"
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

#if defined(MM64)
/*
 * init_pte - Initialize PTE entry
 */
int init_pte(addr_t *pte,
             int pre,    // present
             addr_t fpn,    // FPN
             int drt,    // dirty
             int swp,    // swap
             int swptyp, // swap type
             addr_t swpoff) // swap offset
{
  if (pre != 0) {
    if (swp == 0) { // Non swap ~ page online
      if (fpn == 0)
        return -1;  // Invalid setting

      /* Valid setting with FPN */
      SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
      CLRBIT(*pte, PAGING_PTE_SWAPPED_MASK);
      CLRBIT(*pte, PAGING_PTE_DIRTY_MASK);

      SETVAL(*pte, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);
    }
    else
    { // page swapped
      SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
      SETBIT(*pte, PAGING_PTE_SWAPPED_MASK);
      CLRBIT(*pte, PAGING_PTE_DIRTY_MASK);

      SETVAL(*pte, swptyp, PAGING_PTE_SWPTYP_MASK, PAGING_PTE_SWPTYP_LOBIT);
      SETVAL(*pte, swpoff, PAGING_PTE_SWPOFF_MASK, PAGING_PTE_SWPOFF_LOBIT);
    }
  }

  return 0;
}


/*
 * get_pd_from_pagenum - Parse address to 5 page directory level
 * @pgn   : pagenumer
 * @pgd   : page global directory
 * @p4d   : page level directory
 * @pud   : page upper directory
 * @pmd   : page middle directory
 * @pt    : page table 
 */
int get_pd_from_address(addr_t addr, addr_t* pgd, addr_t* p4d, addr_t* pud, addr_t* pmd, addr_t* pt)
{
  addr_t high = addr >> PAGING64_ADDR_PGD_HIBIT;
  if (high != 0 && high != (UINT64_MAX >> PAGING64_ADDR_PGD_HIBIT))
    return -1;

  *pgd = PAGING64_ADDR_PGD(addr);
  *p4d = PAGING64_ADDR_P4D(addr);
  *pud = PAGING64_ADDR_PUD(addr);
  *pmd = PAGING64_ADDR_PMD(addr);
  *pt = PAGING64_ADDR_PT(addr);
  return 0;
}

/*
 * get_pd_from_pagenum - Parse page number to 5 page directory level
 * @pgn   : pagenumer
 * @pgd   : page global directory
 * @p4d   : page level directory
 * @pud   : page upper directory
 * @pmd   : page middle directory
 * @pt    : page table 
 */
int get_pd_from_pagenum(addr_t pgn, addr_t* pgd, addr_t* p4d, addr_t* pud, addr_t* pmd, addr_t* pt)
{
  /* Shift the address to get page num and perform the mapping*/
  if (pgn > (UINT64_MAX >> PAGING64_ADDR_PT_SHIFT))
    return -1;
  return get_pd_from_address(pgn << PAGING64_ADDR_PT_SHIFT,
                         pgd,p4d,pud,pmd,pt);
}


/*
 * pte_set_swap - Set PTE entry for swapped page
 * @pte    : target page table entry (PTE)
 * @swptyp : swap type
 * @swpoff : swap offset
 */
int pte_set_swap(struct pcb_t *caller, addr_t pgn, int swptyp, addr_t swpoff)
{
  struct krnl_t *krnl = caller->krnl;

  addr_t *pte;
  addr_t pgd=0;
  addr_t p4d=0;
  addr_t pud=0;
  addr_t pmd=0;
  addr_t pt=0;

  // dummy pte alloc to avoid runtime error
  pte = malloc(sizeof(addr_t));
#ifdef MM64	
  /* Get value from the system */
  /* TODO Perform multi-level page mapping */
  get_pd_from_pagenum(pgn, &pgd, &p4d, &pud, &pmd, &pt);
  //... krnl->mm->pgd
  //... krnl->mm->pt
  //pte = &krnl->mm->pt;
#else
  pte = &krnl->mm->pgd[pgn];
#endif

  CLRBIT(*pte, PAGING_PTE_PRESENT_MASK);
  SETBIT(*pte, PAGING_PTE_SWAPPED_MASK);

  SETVAL(*pte, swptyp, PAGING_PTE_SWPTYP_MASK, PAGING_PTE_SWPTYP_LOBIT);
  SETVAL(*pte, swpoff, PAGING_PTE_SWPOFF_MASK, PAGING_PTE_SWPOFF_LOBIT);

  return 0;
}

/* Directory entries point to child tables. Only writes allocate missing tables. */
static addr_t *get_pte_ptr(struct mm_struct *mm, addr_t pgn, bool create)
{
  addr_t indices[PAGING64_LEVELS];
  if (mm == NULL || mm->pgd == NULL ||
      get_pd_from_pagenum(pgn, &indices[0], &indices[1], &indices[2],
                          &indices[3], &indices[4]) != 0)
    return NULL;

  addr_t *table = mm->pgd;
  unsigned int level;
  for (level = 0; level < PAGING64_LEVELS - 1; ++level)
  {
    addr_t *entry = &table[indices[level]];
    if (*entry == 0 && create)
      *entry = (addr_t)(uintptr_t)calloc(PAGING64_TABLE_ENTRIES, sizeof(*table));
    table = (addr_t *)(uintptr_t)*entry;
    if (table == NULL)
      break;
  }

  /* A lookup counts the leaf or the first missing directory entry as well. */
  if (!create)
    __atomic_fetch_add(&mm->pte_lookup_reads, level + 1, __ATOMIC_RELAXED);
  return table == NULL ? NULL : &table[indices[level]];
}

/*
 * pte_set_fpn - Set PTE entry for on-line page
 * @pte   : target page table entry (PTE)
 * @fpn   : frame page number (FPN)
 * @caller: caller
 */
int pte_set_fpn(struct mm_struct *mm, addr_t pgn, addr_t fpn, struct pcb_t *caller)
{
  (void)caller;
  addr_t max_fpn = (addr_t)PAGING_PTE_FPN_MASK >> PAGING_PTE_FPN_LOBIT;
  if (fpn > max_fpn)
    return -1;

  addr_t *pte = get_pte_ptr(mm, pgn, true);
  if (pte == NULL)
    return -1;

  SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
  CLRBIT(*pte, (addr_t)(PAGING_PTE_SWAPPED_MASK | PAGING_PTE_EMPTY01_MASK | PAGING_PTE_EMPTY02_MASK));
  SETVAL(*pte, fpn, (addr_t)PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);

  return 0;
}


addr_t pte_get_entry(struct mm_struct *mm, addr_t pgn, struct pcb_t *caller)
{
  (void)caller;
  const addr_t *pte = get_pte_ptr(mm, pgn, false);
  return pte == NULL ? 0 : *pte;
}

static size_t count_page_tables(const addr_t *table, unsigned int depth)
{
  if (table == NULL)
    return 0;

  size_t count = 1;

  if (depth > 1)
    for (size_t i = 0; i < PAGING64_TABLE_ENTRIES; ++i)
      if (table[i] != 0)
        count += count_page_tables((const addr_t *)(uintptr_t)table[i],
                                   depth - 1);

  return count;
}

/* The caller prevents concurrent changes to the page-table tree. */
size_t mmu_page_table_bytes(const struct mm_struct *mm)
{
  return mm == NULL ? 0 :
         count_page_tables(mm->pgd, PAGING64_LEVELS) * PAGING64_TABLE_ENTRIES * sizeof(addr_t);
}

int pte_set_entry(struct mm_struct *mm, addr_t pgn, uint64_t pte_val, struct pcb_t *caller)
{
  (void)caller;
  addr_t *pte = get_pte_ptr(mm, pgn, true);
  if (pte == NULL)
    return -1;

  *pte = pte_val;
  return 0;
}

/*
 * vmap_pgd_memset - map a range of page at aligned address
 */
int vmap_pgd_memset(struct pcb_t *caller,           // process call
                    addr_t addr,                       // start address which is aligned to pagesz
                    int pgnum)                      // num of mapping page
{
  //int pgit = 0;
  //uint64_t pattern = 0xdeadbeef;

  /* TODO memset the page table with given pattern
   */

  return 0;
}

/*
 * vmap_page_range - map a range of page at aligned address
 */
addr_t vmap_page_range(struct pcb_t *caller,           // process call
                    addr_t addr,                       // start address which is aligned to pagesz // start
                    int pgnum,                      // num of mapping page
                    struct framephy_struct *frames, // list of the mapped frames
                    struct vm_rg_struct *ret_rg)    // return mapped region, the real mapped fp
{                                                   // no guarantee all given pages are mapped
  if (caller == NULL || caller->mm == NULL || caller->krnl == NULL ||
      caller->krnl->mram == NULL || ret_rg == NULL || pgnum <= 0 ||
      addr % PAGING64_PAGESZ != 0)
    return (addr_t)-1;

  addr_t size = (addr_t)pgnum * PAGING64_PAGESZ;
  if (size > UINT64_MAX - addr)
    return (addr_t)-1;

  struct mm_struct *mm = caller->mm;
  struct pgn_t *old_fifo = mm->fifo_pgn;
  struct framephy_struct *fpit = frames;
  addr_t first_pgn = addr / PAGING64_PAGESZ;
  addr_t frames_in_ram = caller->krnl->mram->maxsz / PAGING64_PAGESZ;
  int mapped = 0;

  for (; mapped < pgnum; ++mapped, fpit = fpit->fp_next)
  {
    addr_t pgn = first_pgn + mapped;
    if (fpit == NULL || fpit->owner != mm || fpit->fpn >= frames_in_ram ||
        pte_get_entry(mm, pgn, caller) != 0 ||
        pte_set_fpn(mm, pgn, fpit->fpn, caller) != 0)
      break;
  }

  bool complete = mapped == pgnum && fpit == NULL;
  for (int i = 0; complete && i < pgnum; ++i)
    complete = enlist_pgn_node(&mm->fifo_pgn, first_pgn + i) == 0;

  if (!complete)
  {
    while (mapped > 0)
      pte_set_entry(mm, first_pgn + --mapped, 0, caller);

    while (mm->fifo_pgn != old_fifo)
    {
      struct pgn_t *node = mm->fifo_pgn;
      mm->fifo_pgn = node->pg_next;
      free(node);
    }
    return (addr_t)-1;
  }

  ret_rg->rg_start = addr;
  ret_rg->rg_end = addr + size;
  ret_rg->vmaid = 0;
  ret_rg->mode_bit = 1;
  ret_rg->rg_next = NULL;
  return 0;
}

/*
 * alloc_pages_range - allocate req_pgnum of frame in ram
 * @caller    : caller
 * @req_pgnum : request page num
 * @frm_lst   : frame list
 */

addr_t alloc_pages_range(struct pcb_t *caller, int req_pgnum, struct framephy_struct **frm_lst)
{
  if (frm_lst == NULL)
    return (addr_t)-1;
  *frm_lst = NULL;

  if (caller == NULL || caller->mm == NULL || caller->krnl == NULL ||
      caller->krnl->mram == NULL || req_pgnum <= 0)
    return (addr_t)-1;

  struct memphy_struct *ram = caller->krnl->mram;
  struct framephy_struct *head = ram->free_fp_list;
  struct framephy_struct *tail = head;
  for (int i = 1; i < req_pgnum && tail != NULL; ++i)
    tail = tail->fp_next;
  if (tail == NULL)
    return (addr_t)-3000;

  ram->free_fp_list = tail->fp_next;
  tail->fp_next = NULL;
  for (struct framephy_struct *fp = head; fp != NULL; fp = fp->fp_next)
    fp->owner = caller->mm;

  *frm_lst = head;
  return 0;
}

/*
 * vm_map_ram - do the mapping all vm are to ram storage device
 * @caller    : caller
 * @astart    : vm area start
 * @aend      : vm area end
 * @mapstart  : start mapping point
 * @incpgnum  : number of mapped page
 * @ret_rg    : returned region
 */
addr_t vm_map_ram(struct pcb_t *caller, addr_t astart, addr_t aend, addr_t mapstart, int incpgnum, struct vm_rg_struct *ret_rg)
{
  if (ret_rg == NULL || incpgnum <= 0 || mapstart < astart || mapstart > aend ||
      (addr_t)incpgnum * PAGING64_PAGESZ > aend - mapstart)
    return (addr_t)-1;

  struct framephy_struct *frm_lst = NULL;
  if (alloc_pages_range(caller, incpgnum, &frm_lst) != 0)
    return (addr_t)-1;

  struct memphy_struct *ram = caller->krnl->mram;
  struct framephy_struct *tail = frm_lst;
  while (tail->fp_next != NULL)
    tail = tail->fp_next;

  if (vmap_page_range(caller, mapstart, incpgnum, frm_lst, ret_rg) != 0)
  {
    for (struct framephy_struct *fp = frm_lst; fp != NULL; fp = fp->fp_next)
      fp->owner = NULL;
    tail->fp_next = ram->free_fp_list;
    ram->free_fp_list = frm_lst;
    return (addr_t)-1;
  }

  tail->fp_next = ram->used_fp_list;
  ram->used_fp_list = frm_lst;
  return 0;
}

static int find_free_frame_run(const struct memphy_struct *ram, int pages)
{
  int frame_count = ram->maxsz / PAGING64_PAGESZ;
  const struct framephy_struct *head = ram->free_fp_list;
  if (head == NULL || pages > frame_count)
    return -1;

  if (pages == 1)
    return head->fpn < (addr_t)frame_count ? (int)head->fpn : -1;

  unsigned char *available = calloc(frame_count, sizeof(*available));
  if (available == NULL)
    return -1;

  for (const struct framephy_struct *fp = head; fp != NULL; fp = fp->fp_next)
    if (fp->fpn < (addr_t)frame_count)
      available[fp->fpn] = 1;

  int first = -1;
  int consecutive = 0;
  for (int fpn = 0; fpn < frame_count; ++fpn)
  {
    consecutive = available[fpn] ? consecutive + 1 : 0;
    if (consecutive == pages)
    {
      first = fpn - pages + 1;
      break;
    }
  }

  free(available);
  return first;
}

/* The caller holds both memory mutexes while mapping pages and assigning frames. */
addr_t vm_map_kernel(struct pcb_t *caller, addr_t astart, addr_t aend,
                     addr_t mapstart, int incpgnum, struct vm_rg_struct *ret_rg)
{
  if (caller == NULL || caller->krnl == NULL || caller->krnl->mm == NULL ||
      caller->krnl->mram == NULL || ret_rg == NULL || incpgnum <= 0 ||
      mapstart < astart || mapstart > aend || mapstart % PAGING64_PAGESZ != 0)
    return (addr_t)-1;

  addr_t size = (addr_t)incpgnum * PAGING64_PAGESZ;
  if (size > aend - mapstart)
    return (addr_t)-1;

  struct mm_struct *mm = caller->krnl->mm;
  struct memphy_struct *ram = caller->krnl->mram;
  int first = find_free_frame_run(ram, incpgnum);
  if (first < 0)
    return (addr_t)-1;

  addr_t pgn = mapstart / PAGING64_PAGESZ;
  int mapped = 0;
  for (; mapped < incpgnum; ++mapped)
  {
    if (pte_get_entry(mm, pgn + mapped, caller) != 0 ||
        pte_set_fpn(mm, pgn + mapped, (addr_t)first + mapped, caller) != 0)
      break;
  }

  if (mapped != incpgnum)
  {
    while (mapped > 0)
      pte_set_entry(mm, pgn + --mapped, 0, caller);
    return (addr_t)-1;
  }

  struct framephy_struct **link = &ram->free_fp_list;
  while (*link != NULL)
  {
    struct framephy_struct *fp = *link;
    if (fp->fpn >= (addr_t)first && fp->fpn < (addr_t)first + incpgnum)
    {
      *link = fp->fp_next;
      fp->owner = mm;
      fp->kernel_users = 1;
      fp->fp_next = ram->used_fp_list;
      ram->used_fp_list = fp;
    }
    else
      link = &fp->fp_next;
  }

  *ret_rg = (struct vm_rg_struct){
    .rg_start = mapstart,
    .rg_end = mapstart + size,
    .mode_bit = 0
  };
  return 0;
}

/* Swap copy content page from source frame to destination frame
 * @mpsrc  : source memphy
 * @srcfpn : source physical page number (FPN)
 * @mpdst  : destination memphy
 * @dstfpn : destination physical page number (FPN)
 **/
int __swap_cp_page(struct memphy_struct *mpsrc, addr_t srcfpn,
                   struct memphy_struct *mpdst, addr_t dstfpn)
{
  int cellidx;
  addr_t addrsrc, addrdst;
#ifdef MM64
  for (cellidx = 0; cellidx < PAGING64_PAGESZ; cellidx++)
  {
    addrsrc = srcfpn * PAGING64_PAGESZ + cellidx;
    addrdst = dstfpn * PAGING64_PAGESZ + cellidx;
#else
  for (cellidx = 0; cellidx < PAGING64_PAGESZ; cellidx++)
  {
    addrsrc = srcfpn * PAGING64_PAGESZ + cellidx;
    addrdst = dstfpn * PAGING64_PAGESZ + cellidx;
#endif

    BYTE data;
    MEMPHY_read(mpsrc, addrsrc, &data);
    MEMPHY_write(mpdst, addrdst, data);
  }

  return 0;
}

/* Initialize an empty address space and its first VMA. */
int init_mm(struct mm_struct *mm, struct pcb_t *caller)
{
  if (mm == NULL)
    return -1;

  addr_t first_base = caller == NULL ? UINT64_C(0xffff800000000000) : 0;
  int vma_count = caller == NULL ? 1 : HEAP_VMA_LARGE + 1;

  *mm = (struct mm_struct){0};
  mm->pgd = calloc(PAGING64_TABLE_ENTRIES, sizeof(*mm->pgd));
  if (mm->pgd == NULL)
    return -1;

  for (int id = vma_count - 1; id >= 0; --id)
  {
    struct vm_area_struct *vma = malloc(sizeof(*vma));
    if (vma == NULL)
    {
      while (mm->mmap != NULL)
      {
        vma = mm->mmap;
        mm->mmap = vma->vm_next;
        free(vma);
      }

      free(mm->pgd);
      mm->pgd = NULL;
      return -1;
    }

    addr_t base = first_base + (addr_t)id * PAGING64_HEAP_VMA_SPAN;
    *vma = (struct vm_area_struct){
      .vm_id = id,
      .vm_start = base,
      .vm_end = base,
      .sbrk = base,
      .vm_mm = mm,
      .vm_next = mm->mmap
    };

    mm->mmap = vma;
  }

  return 0;
}

struct vm_rg_struct *init_vm_rg(addr_t rg_start, addr_t rg_end)
{
  struct vm_rg_struct *rgnode = malloc(sizeof(*rgnode));
  if (rgnode == NULL)
    return NULL;

  *rgnode = (struct vm_rg_struct){ .rg_start = rg_start, .rg_end = rg_end };
  return rgnode;
}

int enlist_vm_rg_node(struct vm_rg_struct **rglist, struct vm_rg_struct *rgnode)
{
  rgnode->rg_next = *rglist;
  *rglist = rgnode;

  return 0;
}

int enlist_pgn_node(struct pgn_t **plist, addr_t pgn)
{
  struct pgn_t *pnode = malloc(sizeof(struct pgn_t));
  if (pnode == NULL)
  return -1;

  pnode->pgn = pgn;
  pnode->pg_next = *plist;
  *plist = pnode;

  return 0;
}

int print_list_fp(struct framephy_struct *ifp)
{
  struct framephy_struct *fp = ifp;

  printf("print_list_fp: ");
  if (fp == NULL) { printf("NULL list\n"); return -1;}
  printf("\n");
  while (fp != NULL)
  {
    printf("fp[" FORMAT_ADDR "]\n", fp->fpn);
    fp = fp->fp_next;
  }
  printf("\n");
  return 0;
}

int print_list_rg(struct vm_rg_struct *irg)
{
  struct vm_rg_struct *rg = irg;

  printf("print_list_rg: ");
  if (rg == NULL) { printf("NULL list\n"); return -1; }
  printf("\n");
  while (rg != NULL)
  {
    printf("rg[" FORMAT_ADDR "->"  FORMAT_ADDR "]\n", rg->rg_start, rg->rg_end);
    rg = rg->rg_next;
  }
  printf("\n");
  return 0;
}

int print_list_vma(struct vm_area_struct *ivma)
{
  struct vm_area_struct *vma = ivma;

  printf("print_list_vma: ");
  if (vma == NULL) { printf("NULL list\n"); return -1; }
  printf("\n");
  while (vma != NULL)
  {
    printf("va[" FORMAT_ADDR "->" FORMAT_ADDR "]\n", vma->vm_start, vma->vm_end);
    vma = vma->vm_next;
  }
  printf("\n");
  return 0;
}

int print_list_pgn(struct pgn_t *ip)
{
  printf("print_list_pgn: ");
  if (ip == NULL) { printf("NULL list\n"); return -1; }
  printf("\n");
  while (ip != NULL)
  {
    printf("va[" FORMAT_ADDR "]-\n", ip->pgn);
    ip = ip->pg_next;
  }
  printf("\n");
  return 0;
}

int print_pgtbl(struct pcb_t *caller, addr_t start, addr_t end)
{
//addr_t pgn = ...
//addr_t pgit = ...
//struct mm_struct *mm = ...
  addr_t pgd=0;
  addr_t p4d=0;
  addr_t pud=0;
  addr_t pmd=0;
  addr_t pt=0;

  get_pd_from_address(start, &pgd, &p4d, &pud, &pmd, &pt);

  /* TODO traverse the page map and dump the page directory entries */

  return 0;
}

#endif  //def MM64
