/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Caitoa release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

#include "os-cfg.h"
#include "os-mm.h"
#include "syscall.h"
#include "libmem.h"
#include "sched.h"
#include <pthread.h>

#ifdef MM64
#include "mm64.h"
#else
#include "mm.h"
#endif

extern pthread_mutex_t mmvm_lock;

/* The caller holds mmvm_lock across the ownership check and the byte transfer. */
static int user_frame_io(struct pcb_t *caller, int memop, struct sc_regs *regs)
{
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
  struct framephy_struct *frame = ram->used_fp_list;
  while (frame != NULL && frame->fpn != fpn)
    frame = frame->fp_next;
  if (frame == NULL || frame->owner != caller->mm)
    return -1;

  if (memop == SYSMEM_IO_WRITE)
    return MEMPHY_write(ram, addr, (BYTE)regs->a3);

  BYTE value;
  int status = MEMPHY_read(ram, addr, &value);
  if (status == 0)
    regs->a3 = (unsigned char)value;
  return status;
}

int __sys_memmap(struct krnl_t *krnl, uint32_t pid, struct sc_regs *regs)
{
  if (krnl == NULL || regs == NULL)
    return -1;

  int memop = regs->a1;

  struct pcb_t *caller = find_proc_by_pid_safe(pid);
  if (caller == NULL || caller->krnl != krnl)
    return -1;

  switch (memop)
  {
  case SYSMEM_MAP_OP:
    /* Reserved process case */
    vmap_pgd_memset(caller, regs->a2, regs->a3);
    break;

  case SYSMEM_INC_OP:
  {
    if (regs->a2 > (arg_t)INT32_MAX)
      return -1;

    pthread_mutex_lock(&mmvm_lock);
    int result = inc_vma_limit(caller, (int)regs->a2, regs->a3);
    pthread_mutex_unlock(&mmvm_lock);
    return result;
  }

  case SYSMEM_SWP_OP:
    /* Page replacement is not implemented. */
    return -1;

  case SYSMEM_IO_READ:
  case SYSMEM_IO_WRITE:
  {
    pthread_mutex_lock(&mmvm_lock);
    int result = user_frame_io(caller, memop, regs);
    pthread_mutex_unlock(&mmvm_lock);
    return result;
  }

  default:
    return __sys_kmem(caller, regs);
  }

  return 0;
}
