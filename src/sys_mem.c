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

#ifdef MM64
#include "mm64.h"
#else
#include "mm.h"
#endif

int __sys_memmap(struct krnl_t *krnl, uint32_t pid, struct sc_regs* regs)
{
   if (krnl == NULL || regs == NULL)
      return -1;

   int memop = regs->a1;

   struct pcb_t *caller = find_proc_by_pid_safe(pid);
   if (caller == NULL || caller->krnl != krnl)
      return -1;

   switch (memop) {
   case SYSMEM_MAP_OP:
      /* Reserved process case */
      vmap_pgd_memset(caller, regs->a2, regs->a3);
      break;

   case SYSMEM_INC_OP:
   case SYSMEM_IO_READ:
   case SYSMEM_IO_WRITE:
      return __sys_umem(caller, memop, regs);

   case SYSMEM_SWP_OP:
      /* Page replacement is not implemented. */
      return -1;

   default:
      return __sys_kmem(caller, regs);
   }

   return 0;
}
