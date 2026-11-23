/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 *
 * demacOprx release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 *
 * System Library
 * Memory Module Library libmem.c
 */

#include "mm64.h"
#include "syscall.h"
#include "libmem.h"
#include "sysbus.h"
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>

/* mmvm_lock protects virtual mappings and regions. kmm_lock protects RAM
 * frame lists and transfers through the memory syscalls. Take mmvm_lock first
 * whenever an operation needs both. */
static pthread_mutex_t mmvm_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t kmm_lock = PTHREAD_MUTEX_INITIALIZER;

static int valid_register(const struct pcb_t *proc, arg_t index)
{
  return proc != NULL && index < sizeof(proc->regs) / sizeof(proc->regs[0]);
}

/*enlist_vm_freerg_list - add new rg to freerg_list
 *@mm: memory region
 *@rg_elmt: new region
 *
 */
int enlist_vm_freerg_list(struct mm_struct *mm, struct vm_rg_struct *rg_elmt)
{
  if (rg_elmt == NULL || rg_elmt->rg_start >= rg_elmt->rg_end)
    return -1;

  struct vm_area_struct *vma = get_vma_by_num(mm, rg_elmt->vmaid);
  if (vma == NULL)
    return -1;

  struct vm_rg_struct **link = &vma->vm_freerg_list;
  while (*link != NULL)
  {
    struct vm_rg_struct *rg = *link;

    if (rg->rg_end == rg_elmt->rg_start)
      rg_elmt->rg_start = rg->rg_start;
    else if (rg->rg_start == rg_elmt->rg_end)
      rg_elmt->rg_end = rg->rg_end;
    else
    {
      link = &rg->rg_next;
      continue;
    }

    *link = rg->rg_next;
    free(rg);
    link = &vma->vm_freerg_list;
  }

  return enlist_vm_rg_node(&vma->vm_freerg_list, rg_elmt);
}

/*get_symrg_byid - get mem region by region ID
 *@mm: memory region
 *@rgid: region ID act as symbol index of variable
 *
 */
struct vm_rg_struct *get_symrg_byid(struct mm_struct *mm, int rgid)
{
  if (mm == NULL || rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ)
    return NULL;

  return &mm->symrgtbl[rgid];
}

static int get_region_vmaid(struct pcb_t *proc, int rgid)
{
  pthread_mutex_lock(&mmvm_lock);
  const struct vm_rg_struct *rg = get_symrg_byid(proc->mm, rgid);
  int vmaid = rg != NULL ? rg->vmaid : -1;
  pthread_mutex_unlock(&mmvm_lock);
  return vmaid;
}

#ifdef MM64
void print_mmu_stats(const struct pcb_t *proc)
{
  pthread_mutex_lock(&mmvm_lock);

  struct mm_struct *user_mm = proc->mm;
  struct mm_struct *kernel_mm = proc->krnl->mm;
  struct memphy_struct *ram = proc->krnl->mram;

  size_t user_bytes = mmu_page_table_bytes(user_mm);
  size_t kernel_bytes = mmu_page_table_bytes(kernel_mm);

  uint64_t user_reads =
      __atomic_load_n(&user_mm->pte_lookup_reads, __ATOMIC_RELAXED);
  uint64_t kernel_reads =
      __atomic_load_n(&kernel_mm->pte_lookup_reads, __ATOMIC_RELAXED);
  pthread_mutex_lock(&kmm_lock);
  uint64_t ram_reads =
      __atomic_load_n(&ram->data_reads, __ATOMIC_RELAXED);
  uint64_t ram_writes =
      __atomic_load_n(&ram->data_writes, __ATOMIC_RELAXED);
  pthread_mutex_unlock(&kmm_lock);

  pthread_mutex_unlock(&mmvm_lock);

  printf("MMU pid=%u user_table_bytes=%zu user_lookup_entry_reads=%" PRIu64
         " kernel_table_bytes=%zu kernel_lookup_entry_reads_total=%" PRIu64
         " ram_reads_total=%" PRIu64 " ram_writes_total=%" PRIu64 "\n",
         proc->pid, user_bytes, user_reads, kernel_bytes, kernel_reads,
         ram_reads, ram_writes);
}
#endif

/*__alloc - allocate a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *@alloc_addr: address of allocated memory region
 *
 */
int __alloc(struct pcb_t *caller, int vmaid, int rgid, addr_t size, addr_t *alloc_addr)
{
  if (caller == NULL || alloc_addr == NULL || rgid < 0 ||
      rgid >= PAGING_MAX_SYMTBL_SZ || size == 0 || size > (addr_t)INT32_MAX)
    return -1;

  struct mm_struct *mm = caller->mm;
  struct vm_area_struct *cur_vma = get_vma_by_num(mm, vmaid);
  if (cur_vma == NULL)
    return -1;

  struct vm_rg_struct *entry = &mm->symrgtbl[rgid];
  struct vm_rg_struct rgnode;

  pthread_mutex_lock(&mmvm_lock);
  if (entry->rg_start != entry->rg_end)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  if (get_free_vmrg_area(caller, vmaid, (int)size, &rgnode) != 0)
  {
    struct sc_regs regs = {.a1 = SYSMEM_INC_OP, .a2 = vmaid, .a3 = size};

    pthread_mutex_unlock(&mmvm_lock);
    if (_syscall(caller->krnl, caller->pid, 17, &regs) != 0)
      return -1;

    pthread_mutex_lock(&mmvm_lock);
    if (entry->rg_start != entry->rg_end ||
        get_free_vmrg_area(caller, vmaid, (int)size, &rgnode) != 0)
    {
      pthread_mutex_unlock(&mmvm_lock);
      return -1;
    }
  }

  *entry = rgnode;
  if (cur_vma->sbrk < rgnode.rg_end)
    cur_vma->sbrk = rgnode.rg_end;

  *alloc_addr = rgnode.rg_start;
  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/* Called with mmvm_lock held. */
static int release_user_region(struct mm_struct *mm, int vmaid, int rgid)
{
  struct vm_rg_struct *rgnode = get_symrg_byid(mm, rgid);
  if (rgnode == NULL || rgnode->vmaid != vmaid || rgnode->mode_bit != 1 ||
      rgnode->rg_start >= rgnode->rg_end)
    return -1;

  struct vm_rg_struct *freerg_node = init_vm_rg(rgnode->rg_start, rgnode->rg_end);
  if (freerg_node == NULL)
    return -1;

  freerg_node->vmaid = vmaid;
  freerg_node->mode_bit = 1;
  if (enlist_vm_freerg_list(mm, freerg_node) != 0)
  {
    free(freerg_node);
    return -1;
  }

  *rgnode = (struct vm_rg_struct){0};
  return 0;
}

int __free(struct pcb_t *caller, int vmaid, int rgid)
{
  if (caller == NULL || vmaid < 0)
    return -1;

  pthread_mutex_lock(&mmvm_lock);
  int result = dma_regions_busy(caller->pid, rgid, rgid) ? -EAGAIN
                                                      : release_user_region(caller->mm, vmaid, rgid);
  pthread_mutex_unlock(&mmvm_lock);
  return result;
}

/*liballoc - PAGING-based allocate a region memory
 *@proc:  Process executing the instruction
 *@size: allocated size
 *@reg_index: memory region ID (used to identify variable in symbole table)
 */
int liballoc(struct pcb_t *proc, addr_t size, uint32_t reg_index)
{
  if (!valid_register(proc, reg_index))
    return -1;

  int vmaid = HEAP_VMA_SMALL;
  if (size > PAGING64_PAGESZ)
    vmaid = HEAP_VMA_LARGE;
  else if (size > 1024)
    vmaid = HEAP_VMA_MEDIUM;

  addr_t addr;
  int val = __alloc(proc, vmaid, reg_index, size, &addr);
  if (val != 0)
    return val;

  proc->regs[reg_index] = addr;

#ifdef IODUMP
  printf("%s:%d pid=%u pc=%u alloc reg=%u size=%lu addr=0x%lx\n",
         __func__, __LINE__, proc->pid, proc->pc, reg_index,
         (unsigned long)size, (unsigned long)addr);

#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1);
#endif
#endif

  return val;
}

/*libfree - PAGING-based free a region memory
 *@proc: Process executing the instruction
 *@size: allocated size
 *@reg_index: memory region ID (used to identify variable in symbole table)
 */

int libfree(struct pcb_t *proc, uint32_t reg_index)
{
  if (!valid_register(proc, reg_index))
    return -1;

  pthread_mutex_lock(&mmvm_lock);

  const struct vm_rg_struct *region = get_symrg_byid(proc->mm, reg_index);
  if (region == NULL || region->rg_start >= region->rg_end)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  int vmaid = region->vmaid;
  int kernel_region = region->mode_bit == 0;
  pthread_mutex_unlock(&mmvm_lock);

  int val;
  if (kernel_region)
  {
    struct sc_regs regs = {.a1 = SYSMEM_KFREE_OP, .a2 = reg_index};
    val = _syscall(proc->krnl, proc->pid, 17, &regs);
  }
  else
    val = __free(proc, vmaid, reg_index);

  if (val != 0)
    return val;

  proc->regs[reg_index] = 0;

#ifdef IODUMP
  printf("%s:%d pid=%u pc=%u free reg=%u\n",
         __func__, __LINE__, proc->pid, proc->pc, reg_index);

#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1);
#endif
#endif

  return val;
}

/*pg_getpage - get the page in ram
 *@mm: memory region
 *@pagenum: PGN
 *@framenum: return FPN
 *@caller: caller
 *
 */
int pg_getpage(struct mm_struct *mm, addr_t pgn, addr_t *fpn, struct pcb_t *caller)
{
  if (mm == NULL || fpn == NULL || caller == NULL)
    return -1;

  pthread_mutex_lock(&mmvm_lock);
  addr_t pte = pte_get_entry(mm, pgn, caller);
  pthread_mutex_unlock(&mmvm_lock);

  if (!PAGING_PAGE_PRESENT(pte))
    return -1;

  *fpn = PAGING_PTE_FPN(pte);
  return 0;
}

/*pg_getval - read value at given offset
 *@mm: memory region
 *@addr: virtual address to acess
 *@value: value
 *
 */
int pg_getval(struct mm_struct *mm, addr_t addr, BYTE *data, struct pcb_t *caller)
{
  addr_t pgn = addr / PAGING64_PAGESZ;
  addr_t off = addr % PAGING64_PAGESZ;
  addr_t fpn;

  if (data == NULL || pg_getpage(mm, pgn, &fpn, caller) != 0)
    return -1;

  addr_t phyaddr = fpn * PAGING64_PAGESZ + off;
  struct sc_regs regs = {.a1 = SYSMEM_IO_READ, .a2 = phyaddr};

  int status = _syscall(caller->krnl, caller->pid, 17, &regs);
  if (status != 0)
    return status;

  *data = (BYTE)regs.a3;
  return 0;
}

/*pg_setval - write value to given offset
 *@mm: memory region
 *@addr: virtual address to acess
 *@value: value
 *
 */
int pg_setval(struct mm_struct *mm, addr_t addr, BYTE value, struct pcb_t *caller)
{
  addr_t pgn = addr / PAGING64_PAGESZ;
  addr_t off = addr % PAGING64_PAGESZ;
  addr_t fpn;

  if (pg_getpage(mm, pgn, &fpn, caller) != 0)
    return -1;

  addr_t phyaddr = fpn * PAGING64_PAGESZ + off;
  struct sc_regs regs = {
    .a1 = SYSMEM_IO_WRITE,
    .a2 = phyaddr,
    .a3 = (unsigned char)value
  };

  return _syscall(caller->krnl, caller->pid, 17, &regs);
}

static int get_user_address(struct pcb_t *caller, int vmaid, int rgid,
                            addr_t offset, addr_t *addr)
{
  if (caller == NULL || vmaid < 0)
    return -1;

  int result = -1;
  pthread_mutex_lock(&mmvm_lock);

  const struct vm_rg_struct *rg = get_symrg_byid(caller->mm, rgid);
  if (rg != NULL && rg->mode_bit == 1 && rg->vmaid == vmaid &&
      rg->rg_start < rg->rg_end &&
      rg->rg_end <= BIT_ULL(PAGING64_CPU_BUS_WIDTH - 1) &&
      offset < rg->rg_end - rg->rg_start)
  {
    *addr = rg->rg_start + offset;
    result = 0;
  }

  pthread_mutex_unlock(&mmvm_lock);
  return result;
}

/*__read - read value in region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@offset: offset to acess in memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __read(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
{
  addr_t addr;
  if (get_user_address(caller, vmaid, rgid, offset, &addr) != 0)
    return -1;

  return pg_getval(caller->mm, addr, data, caller);
}

/*libread - PAGING-based read a region memory */
int libread(
    struct pcb_t *proc, // Process executing the instruction
    uint32_t source,    // Index of source register
    addr_t offset,    // Source address = [source] + [offset]
    uint32_t* destination)
{
  if (!valid_register(proc, source) || destination == NULL)
    return -1;

  BYTE data;
  printf("%s:%d\n", __func__, __LINE__);
  int val = __read(proc, get_region_vmaid(proc, source), source, offset, &data);
  if (val != 0)
    return val;

  *destination = (unsigned char)data;

#if defined(IODUMP) && defined(PAGETBL_DUMP)
  print_pgtbl(proc, 0, -1);
#endif

  return val;
}

/*__write - write a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@offset: offset to acess in memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __write(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value)
{
  addr_t addr;
  if (get_user_address(caller, vmaid, rgid, offset, &addr) != 0)
    return -1;

  return pg_setval(caller->mm, addr, value, caller);
}

/*libwrite - PAGING-based write a region memory */
int libwrite(
    struct pcb_t *proc,   // Process executing the instruction
    BYTE data,            // Data to be wrttien into memory
    uint32_t destination, // Index of destination register
    addr_t offset)
{
  if (!valid_register(proc, destination))
    return -1;

  int val = __write(proc, get_region_vmaid(proc, destination), destination, offset, data);
  if (val != 0)
    return val;

#if defined(IODUMP) && defined(PAGETBL_DUMP)
  print_pgtbl(proc, 0, -1);
#endif

  return val;
}


/*libkmem_malloc- alloc region memory in kmem
 *@caller: caller
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: memory size
 */

int libkmem_malloc(struct pcb_t * caller, uint32_t size, uint32_t reg_index)
{
  if (!valid_register(caller, reg_index))
    return -1;

  struct sc_regs regs = {.a1 = SYSMEM_KMALLOC_OP, .a2 = size, .a3 = reg_index};
  if (_syscall(caller->krnl, caller->pid, 17, &regs) != 0)
    return -1;

  caller->regs[reg_index] = regs.a4;
  return 0;
}


static struct framephy_struct **find_used_frame(struct memphy_struct *ram,
                                               struct mm_struct *owner,
                                               addr_t fpn)
{
  struct framephy_struct **link = &ram->used_fp_list;
  while (*link != NULL && ((*link)->owner != owner || (*link)->fpn != fpn))
    link = &(*link)->fp_next;
  return link;
}

/* Called by __sys_memmap after resolving the caller's PID. */
int __sys_umem(struct pcb_t *caller, int memop, struct sc_regs *regs)
{
  if (memop == SYSMEM_INC_OP)
  {
    if (regs->a2 > (arg_t)INT32_MAX)
      return -1;

    pthread_mutex_lock(&mmvm_lock);
    pthread_mutex_lock(&kmm_lock);
    int result = inc_vma_limit(caller, (int)regs->a2, regs->a3);
    pthread_mutex_unlock(&kmm_lock);
    pthread_mutex_unlock(&mmvm_lock);
    return result;
  }

  struct memphy_struct *ram = caller->krnl->mram;
  addr_t addr = regs->a2;
  if (caller->mm == NULL || ram == NULL || ram->storage == NULL ||
      ram->maxsz <= 0 || addr >= (addr_t)ram->maxsz)
    return -1;

#ifdef MM64
  addr_t fpn = addr / PAGING64_PAGESZ;
#else
  addr_t fpn = addr / PAGING_PAGESZ;
#endif
  int result = -1;
  pthread_mutex_lock(&kmm_lock);
  if (*find_used_frame(ram, caller->mm, fpn) != NULL)
  {
    if (dma_address_busy(caller->pid, addr, memop == SYSMEM_IO_WRITE))
      result = -EAGAIN;
    else if (memop == SYSMEM_IO_WRITE)
      result = MEMPHY_write(ram, addr, (BYTE)regs->a3);
    else
    {
      BYTE value;
      result = MEMPHY_read(ram, addr, &value);
      if (result == 0)
        regs->a3 = (unsigned char)value;
    }
  }
  pthread_mutex_unlock(&kmm_lock);
  return result;
}

/* Return the insertion link and address in the descending list of live regions.
 * Prefer space on a mapped page. Multi-page buffers get fresh, contiguous frames. */
static struct vm_rg_struct **find_kernel_gap(struct vm_area_struct *vma,
                                             addr_t size, addr_t *start)
{
  struct vm_rg_struct **head = &vma->vm_kernel_regions;
  if (size <= vma->vm_end - vma->sbrk)
  {
    *start = vma->sbrk;
    return head;
  }

  struct vm_rg_struct **unused_pages = head;
  *start = vma->vm_end;
  for (struct vm_rg_struct *upper = *head; upper != NULL; upper = upper->rg_next)
  {
    struct vm_rg_struct **link = &upper->rg_next;
    addr_t low = *link != NULL ? (*link)->rg_end : vma->vm_start;
    addr_t high = upper->rg_start;
    if (size > high - low)
      continue;

    addr_t aligned_low = PAGING64_PAGE_ALIGNSZ(low);
    addr_t aligned_high = high - high % PAGING64_PAGESZ;
    if (size <= aligned_low - low)
    {
      *start = low;
      return link;
    }
    if (aligned_high >= low && size <= high - aligned_high)
    {
      *start = aligned_high;
      return link;
    }
    if (unused_pages == head && aligned_high >= aligned_low &&
        size <= aligned_high - aligned_low)
    {
      *start = aligned_low;
      unused_pages = link;
    }
  }
  return unused_pages;
}

/* Called with mmvm_lock and kmm_lock held. */
static addr_t alloc_kernel_region(struct pcb_t *caller, struct vm_area_struct *vma,
                                  struct vm_rg_struct *entry, addr_t size, addr_t *alloc_addr)
{
  if (entry->rg_start != entry->rg_end)
    return (addr_t)-1;

  addr_t start;
  struct vm_rg_struct **link = find_kernel_gap(vma, size, &start);
  if (size > vma->vm_start + PAGING64_HEAP_VMA_SPAN - start)
    return (addr_t)-1;

  addr_t end = start + size;
  addr_t map_end = PAGING64_PAGE_ALIGNSZ(end);

  struct mm_struct *mm = caller->krnl->mm;
  addr_t pte = pte_get_entry(mm, start / PAGING64_PAGESZ, caller);
  if (pte == 0)
  {
    int pages = (map_end - start) / PAGING64_PAGESZ;
    struct vm_rg_struct mapped;

    if (vm_map_kernel(caller, vma->vm_start, map_end, start,
                      pages, &mapped) != 0)
      return (addr_t)-1;

    if (map_end > vma->vm_end)
      vma->vm_end = map_end;
  }
  else
  {
    struct framephy_struct **frame =
        find_used_frame(caller->krnl->mram, mm, PAGING_PTE_FPN(pte));

    if (!PAGING_PAGE_PRESENT(pte) || *frame == NULL ||
        size > PAGING64_PAGESZ - start % PAGING64_PAGESZ)
      return (addr_t)-1;

    ++(*frame)->kernel_users;
  }

  *entry = (struct vm_rg_struct){
    .vmaid = vma->vm_id,
    .rg_start = start,
    .rg_end = end,
    .mode_bit = 0,
    .rg_next = *link
  };

  *link = entry;
  vma->sbrk = vma->vm_kernel_regions->rg_end;
  *alloc_addr = start;
  return 0;
}

addr_t __kmalloc(struct pcb_t *caller, int vmaid, int rgid,
                 addr_t size, addr_t *alloc_addr)
{
  if (caller == NULL || caller->krnl == NULL || caller->krnl->mram == NULL ||
      alloc_addr == NULL || size == 0)
    return (addr_t)-1;

  struct vm_rg_struct *entry = get_symrg_byid(caller->mm, rgid);
  struct vm_area_struct *vma = get_vma_by_num(caller->krnl->mm, vmaid);
  if (entry == NULL || vma == NULL)
    return (addr_t)-1;

  pthread_mutex_lock(&mmvm_lock);
  pthread_mutex_lock(&kmm_lock);
  addr_t result = alloc_kernel_region(caller, vma, entry, size, alloc_addr);
  pthread_mutex_unlock(&kmm_lock);
  pthread_mutex_unlock(&mmvm_lock);
  return result;
}

static int libkmem_copy(struct pcb_t *caller, arg_t op, uint32_t source,
                        uint32_t destination, uint32_t offset, uint32_t size)
{
  if (caller == NULL)
    return -1;

  struct sc_regs regs = {
    .a1 = op,
    .a2 = source,
    .a3 = destination,
    .a4 = offset,
    .a5 = size
  };
  return _syscall(caller->krnl, caller->pid, 17, &regs);
}

int libmem_dma_memcpy(struct pcb_t *caller, uint32_t source, uint32_t destination,
                      uint32_t offset, uint32_t size)
{
  return libkmem_copy(caller, SYSMEM_DMA_COPY, source, destination, offset, size);
}

int libkmem_copy_from_user(struct pcb_t *caller, uint32_t source,
                          uint32_t destination, uint32_t offset, uint32_t size)
{
  return libkmem_copy(caller, SYSMEM_COPY_FROM_USER, source, destination, offset, size);
}

int libkmem_copy_to_user(struct pcb_t *caller, uint32_t source,
                        uint32_t destination, uint32_t offset, uint32_t size)
{
  return libkmem_copy(caller, SYSMEM_COPY_TO_USER, source, destination, offset, size);
}

static int contains_range(const struct vm_rg_struct *rg, addr_t offset, addr_t size)
{
  if (rg == NULL || rg->rg_start >= rg->rg_end)
    return 0;
  addr_t length = rg->rg_end - rg->rg_start;
  return offset <= length && size <= length - offset;
}

/* Called under mmvm_lock and kmm_lock. */
static int get_copy_range(struct pcb_t *caller, int rgid, int vmaid,
                          unsigned long mode_bit, addr_t offset,
                          addr_t size, addr_t *physical)
{
  if (caller == NULL || caller->krnl == NULL)
    return -1;

  struct memphy_struct *ram = caller->krnl->mram;
  if (ram == NULL || ram->storage == NULL || ram->maxsz <= 0)
    return -1;

  const struct vm_rg_struct *rg = get_symrg_byid(caller->mm, rgid);
  if (!contains_range(rg, offset, size) || rg->vmaid != vmaid || rg->mode_bit != mode_bit)
    return -1;

  if (size == 0)
    return 0;

  addr_t start = rg->rg_start + offset;
  addr_t first = start / PAGING64_PAGESZ;
  addr_t last = (start + size - 1) / PAGING64_PAGESZ;
  struct mm_struct *mm = mode_bit == 1 ? caller->mm : caller->krnl->mm;
  addr_t frames_in_ram = ram->maxsz / PAGING64_PAGESZ;

  for (addr_t pgn = first; pgn <= last; ++pgn)
  {
    addr_t pte = pte_get_entry(mm, pgn, caller);
    addr_t fpn = PAGING_PTE_FPN(pte);
    if (!PAGING_PAGE_PRESENT(pte) || fpn >= frames_in_ram)
      return -1;

    if (physical != NULL && pgn == first)
      *physical = fpn * PAGING64_PAGESZ + start % PAGING64_PAGESZ;
  }

  return 0;
}

/* Each descriptor stays within one page at both ends. Publish the whole list
 * only after every translation succeeded, so rejection cannot start a copy. */
static int submit_dma_copy(struct pcb_t *caller, int source, int destination,
                           addr_t offset, addr_t size)
{
  const struct vm_rg_struct *src = get_symrg_byid(caller->mm, source);
  const struct vm_rg_struct *dst = get_symrg_byid(caller->mm, destination);
  if (size == 0 || !contains_range(src, offset, size) || !contains_range(dst, offset, size))
    return -1;

  /* At most one extra boundary per end for each page of data. */
  size_t capacity = 2 * (size / PAGING64_PAGESZ + 1) + 1;
  struct dma_segment *segments = calloc(capacity, sizeof(*segments));
  if (segments == NULL)
    return -1;

  size_t count = 0;
  for (addr_t done = 0; done < size; ++count)
  {
    addr_t pos = offset + done;
    addr_t src_left = PAGING64_PAGESZ - (src->rg_start + pos) % PAGING64_PAGESZ;
    addr_t dst_left = PAGING64_PAGESZ - (dst->rg_start + pos) % PAGING64_PAGESZ;
    addr_t length = src_left < dst_left ? src_left : dst_left;
    if (length > size - done)
      length = size - done;

    addr_t src_pa, dst_pa;
    if (get_copy_range(caller, source, src->vmaid, src->mode_bit, pos, length, &src_pa) != 0 ||
        get_copy_range(caller, destination, dst->vmaid, dst->mode_bit, pos, length, &dst_pa) != 0)
    {
      free(segments);
      return -1;
    }
    segments[count] = (struct dma_segment){ src_pa, dst_pa, length };
    done += length;
  }
  int result = dma_submit(caller->pid, source, destination, segments, count);

  if (result != 0)
    free(segments);
  return result;
}

/* Called with mmvm_lock and kmm_lock held. */
static int copy_region_data(struct pcb_t *caller, int source, int destination,
                            addr_t offset, addr_t size, int to_user)
{
  int user_rgid = to_user ? destination : source;
  const struct vm_rg_struct *user_rg = get_symrg_byid(caller->mm, user_rgid);
  int kernel_rgid = to_user ? source : destination;

  /* Check both complete ranges before writing the first byte. */
  if (user_rg == NULL || user_rg->vmaid < 0 ||
      get_copy_range(caller, user_rgid, user_rg->vmaid, 1, offset, size, NULL) != 0 ||
      get_copy_range(caller, kernel_rgid, 0, 0, offset, size, NULL) != 0)
    return -1;

  if (dma_regions_busy(caller->pid, source, destination))
    return -EAGAIN;

  int user_vmaid = user_rg->vmaid;
  for (addr_t i = 0; i < size; ++i)
  {
    int result;
    BYTE data;
    addr_t pos = offset + i;
    if (to_user)
    {
      result = __read_kernel_mem(caller, 0, source, pos, &data);
      if (result == 0)
        result = __write_user_mem(caller, user_vmaid, destination, pos, data);
    }
    else
    {
      result = __read_user_mem(caller, user_vmaid, source, pos, &data);
      if (result == 0)
        result = __write_kernel_mem(caller, 0, destination, pos, data);
    }
    if (result != 0)
      return result;
  }
  return 0;
}

static int __kmem_copy(struct pcb_t *caller, int source, int destination,
                       addr_t offset, addr_t size, int to_user)
{
  if (caller == NULL)
    return -1;

  pthread_mutex_lock(&mmvm_lock);
  pthread_mutex_lock(&kmm_lock);
  int result = copy_region_data(caller, source, destination, offset, size, to_user);
  pthread_mutex_unlock(&kmm_lock);
  pthread_mutex_unlock(&mmvm_lock);
  return result;
}

static int release_kernel_region(struct pcb_t *caller, struct vm_rg_struct *region);

static int free_kernel_region_by_id(struct pcb_t *caller, int rgid)
{
  pthread_mutex_lock(&mmvm_lock);
  pthread_mutex_lock(&kmm_lock);

  struct vm_rg_struct *region = get_symrg_byid(caller->mm, rgid);
  int status = dma_regions_busy(caller->pid, rgid, rgid) ? -EAGAIN
                                                      : release_kernel_region(caller, region);
  pthread_mutex_unlock(&kmm_lock);
  pthread_mutex_unlock(&mmvm_lock);
  return status;
}

/* Called by __sys_memmap after resolving the caller's PID. */
int __sys_kmem(struct pcb_t *caller, struct sc_regs *regs)
{
  switch (regs->a1)
  {
  case SYSMEM_KMALLOC_OP:
    if (!valid_register(caller, regs->a3))
      return -1;
    return __kmalloc(caller, 0, (int)regs->a3, regs->a2, &regs->a4) == 0 ? 0 : -1;

  case SYSMEM_KFREE_OP:
    if (!valid_register(caller, regs->a2))
      return -1;
    return free_kernel_region_by_id(caller, (int)regs->a2);

  case SYSMEM_DMA_COPY:
  {
    if (!valid_register(caller, regs->a2) || !valid_register(caller, regs->a3) ||
        regs->a4 > UINT32_MAX || regs->a5 > UINT32_MAX)
      return -1;
    pthread_mutex_lock(&mmvm_lock);
    pthread_mutex_lock(&kmm_lock);
    int status = submit_dma_copy(caller, (int)regs->a2, (int)regs->a3, regs->a4, regs->a5);
    pthread_mutex_unlock(&kmm_lock);
    pthread_mutex_unlock(&mmvm_lock);
    return status;
  }
  case SYSMEM_COPY_FROM_USER:
  case SYSMEM_COPY_TO_USER:
    if (!valid_register(caller, regs->a2) || !valid_register(caller, regs->a3))
      return -1;
    return __kmem_copy(caller, (int)regs->a2, (int)regs->a3,
                       regs->a4, regs->a5, regs->a1 == SYSMEM_COPY_TO_USER);

  default:
    printf("Memop code: %d\n", (int)regs->a1);
    return 0;
  }
}

int __read_kernel_mem(struct pcb_t *caller, int vmaid, int rgid,
                      addr_t offset, BYTE *data)
{
  addr_t physical;
  if (data == NULL || get_copy_range(caller, rgid, vmaid, 0, offset, 1, &physical) != 0)
    return -1;

  return MEMPHY_read(caller->krnl->mram, physical, data);
}

int __write_kernel_mem(struct pcb_t *caller, int vmaid, int rgid,
                       addr_t offset, BYTE value)
{
  addr_t physical;
  if (get_copy_range(caller, rgid, vmaid, 0, offset, 1, &physical) != 0)
    return -1;

  return MEMPHY_write(caller->krnl->mram, physical, value);
}

int __read_user_mem(struct pcb_t *caller, int vmaid, int rgid,
                    addr_t offset, BYTE *data)
{
  addr_t physical;
  if (data == NULL || vmaid < 0 ||
      get_copy_range(caller, rgid, vmaid, 1, offset, 1, &physical) != 0)
    return -1;

  return MEMPHY_read(caller->krnl->mram, physical, data);
}

int __write_user_mem(struct pcb_t *caller, int vmaid, int rgid,
                     addr_t offset, BYTE value)
{
  addr_t physical;
  if (vmaid < 0 || get_copy_range(caller, rgid, vmaid, 1, offset, 1, &physical) != 0)
    return -1;

  return MEMPHY_write(caller->krnl->mram, physical, value);
}

/* The caller holds kmm_lock while releasing frames. */
static void release_frame(struct memphy_struct *ram, struct framephy_struct **link)
{
  struct framephy_struct *frame = *link;
  *link = frame->fp_next;
  memset(ram->storage + frame->fpn * PAGING64_PAGESZ, 0, PAGING64_PAGESZ);
  frame->owner = NULL;
  frame->kernel_users = 0;
  frame->fp_next = ram->free_fp_list;
  ram->free_fp_list = frame;
}

/* The caller holds both memory mutexes for the whole release. */
static int release_kernel_region(struct pcb_t *caller,
                                 struct vm_rg_struct *region)
{
  struct krnl_t *kernel = caller->krnl;
  if (region == NULL || region->mode_bit != 0 || kernel == NULL ||
      kernel->mram == NULL || kernel->mram->storage == NULL || kernel->mram->maxsz <= 0)
    return -1;

  struct memphy_struct *ram = kernel->mram;
  struct mm_struct *kernel_mm = kernel->mm;
  struct vm_area_struct *vma = get_vma_by_num(kernel_mm, region->vmaid);

  if (vma == NULL || region->rg_start < vma->vm_start ||
      region->rg_start >= region->rg_end || region->rg_end > vma->vm_end)
    return -1;

  struct vm_rg_struct **link = &vma->vm_kernel_regions;
  while (*link != NULL && *link != region)
    link = &(*link)->rg_next;

  if (*link == NULL)
    return -1;

  addr_t first = region->rg_start / PAGING64_PAGESZ;
  addr_t last = (region->rg_end - 1) / PAGING64_PAGESZ;
  addr_t frame_count = ram->maxsz / PAGING64_PAGESZ;

  for (addr_t pgn = first; pgn <= last; ++pgn)
  {
    addr_t pte = pte_get_entry(kernel_mm, pgn, caller);
    addr_t fpn = PAGING_PTE_FPN(pte);

    if (!PAGING_PAGE_PRESENT(pte) || fpn >= frame_count)
      return -1;

    struct framephy_struct **frame = find_used_frame(ram, kernel_mm, fpn);
    if (*frame == NULL || (*frame)->kernel_users == 0)
      return -1;
  }

  for (addr_t pgn = first; pgn <= last; ++pgn)
  {
    addr_t pte = pte_get_entry(kernel_mm, pgn, caller);
    struct framephy_struct **frame =
        find_used_frame(ram, kernel_mm, PAGING_PTE_FPN(pte));

    if ((*frame)->kernel_users > 1)
    {
      addr_t start = pgn == first ? region->rg_start % PAGING64_PAGESZ : 0;
      addr_t end = pgn == last ? (region->rg_end - 1) % PAGING64_PAGESZ + 1
                              : PAGING64_PAGESZ;

      memset(ram->storage + (*frame)->fpn * PAGING64_PAGESZ + start,
             0, end - start);
      --(*frame)->kernel_users;
    }
    else
    {
      if (pte_set_entry(kernel_mm, pgn, 0, caller) != 0)
        return -1;

      release_frame(ram, frame);
    }
  }

  *link = region->rg_next;
  *region = (struct vm_rg_struct){0};

  while (vma->vm_end > vma->vm_start &&
         pte_get_entry(kernel_mm, vma->vm_end / PAGING64_PAGESZ - 1, caller) == 0)
    vma->vm_end -= PAGING64_PAGESZ;

  vma->sbrk = vma->vm_kernel_regions != NULL ? vma->vm_kernel_regions->rg_end
                                            : vma->vm_start;

  return 0;
}

static int release_kernel_regions(struct pcb_t *caller)
{
  for (int id = 0; id < PAGING_MAX_SYMTBL_SZ; ++id)
  {
    struct vm_rg_struct *region = &caller->mm->symrgtbl[id];

    if (region->mode_bit != 0 || region->rg_start >= region->rg_end)
      continue;
    if (release_kernel_region(caller, region) != 0)
      return -1;
  }
  return 0;
}

static void release_page_tables(addr_t *table, int levels)
{
  if (table == NULL)
    return;

  if (levels > 1)
  {
    for (int i = 0; i < PAGING64_TABLE_ENTRIES; ++i)
      if (table[i] != 0)
        release_page_tables((addr_t *)(uintptr_t)table[i], levels - 1);
  }
  free(table);
}

int free_pcb_memph(struct pcb_t *caller)
{
  if (caller == NULL || caller->mm == NULL)
    return -1;
  struct mm_struct *mm = caller->mm;
  if (caller->krnl != NULL && mm == caller->krnl->mm)
    return -1;

  struct memphy_struct *ram = caller->krnl != NULL ? caller->krnl->mram : NULL;
  pthread_mutex_lock(&mmvm_lock);
  pthread_mutex_lock(&kmm_lock);
  if (dma_regions_busy(caller->pid, -1, -1))
  {
    pthread_mutex_unlock(&kmm_lock);
    pthread_mutex_unlock(&mmvm_lock);
    return -EAGAIN;
  }
  if (release_kernel_regions(caller) != 0)
  {
    pthread_mutex_unlock(&kmm_lock);
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  if (ram != NULL)
  {
    struct framephy_struct **link = &ram->used_fp_list;
    while (*link != NULL)
    {
      if ((*link)->owner == mm)
        release_frame(ram, link);
      else
        link = &(*link)->fp_next;
    }
  }
  pthread_mutex_unlock(&kmm_lock);

  while (mm->mmap != NULL)
  {
    struct vm_area_struct *vma = mm->mmap;
    mm->mmap = vma->vm_next;
    while (vma->vm_freerg_list != NULL)
    {
      struct vm_rg_struct *rg = vma->vm_freerg_list;
      vma->vm_freerg_list = rg->rg_next;
      free(rg);
    }
    free(vma);
  }

  while (mm->fifo_pgn != NULL)
  {
    struct pgn_t *page = mm->fifo_pgn;
    mm->fifo_pgn = page->pg_next;
    free(page);
  }

  release_page_tables(mm->pgd, PAGING64_LEVELS);
  free(mm->kcpooltbl);
  *mm = (struct mm_struct){0};

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}


/*find_victim_page - find victim page
 *@caller: caller
 *@pgn: return page number
 *
 */
int find_victim_page(struct mm_struct *mm, addr_t *retpgn)
{
  struct pgn_t *pg = mm->fifo_pgn;

  /* TODO: Implement the theorical mechanism to find the victim page */
  if (!pg)
  {
    return -1;
  }
  struct pgn_t *prev = NULL;
  while (pg->pg_next)
  {
    prev = pg;
    pg = pg->pg_next;
  }
  *retpgn = pg->pgn;
  prev->pg_next = NULL;

  free(pg);

  return 0;
}

/*get_free_vmrg_area - get a free vm region
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@size: allocated size
 *
 */
int get_free_vmrg_area(struct pcb_t *caller, int vmaid, int size,
                       struct vm_rg_struct *newrg)
{
  if (caller == NULL || newrg == NULL || size <= 0)
    return -1;

  struct vm_area_struct *vma = get_vma_by_num(caller->mm, vmaid);
  if (vma == NULL)
    return -1;

  struct vm_rg_struct **link = &vma->vm_freerg_list;
  addr_t needed = (addr_t)size;

  while (*link != NULL)
  {
    struct vm_rg_struct *rg = *link;
    if (rg->rg_start <= rg->rg_end && needed <= rg->rg_end - rg->rg_start)
    {
      *newrg = (struct vm_rg_struct){
        .vmaid = vmaid,
        .rg_start = rg->rg_start,
        .rg_end = rg->rg_start + needed,
        .mode_bit = 1
      };

      if (newrg->rg_end == rg->rg_end)
      {
        *link = rg->rg_next;
        free(rg);
      }
      else
      {
        rg->rg_start = newrg->rg_end;
      }

      return 0;
    }

    link = &rg->rg_next;
  }

  return -1;
}
