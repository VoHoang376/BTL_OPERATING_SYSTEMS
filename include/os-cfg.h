#ifndef OS_CFG_H
#define OS_CFG_H

/* Configuration constants for Simple OS simulation */

/* Scheduler Configuration */
#define MLQ_SCHED 1
#define MAX_PRIO 140

/* Memory Management Configuration */
#define MM_PAGING 1
#define MM_FIXED_MEMSZ 1
#define MM64 1

/* Memory limits and parameters */
#define PAGING_MAX_SYMTBL_SZ 30
#define PAGING_MAX_MMSWP 4
#define PAGING_PAGESZ 4096       /* 4KB page size for 64-bit multi-level paging */
#define PAGING_MAX_PGN 0x100000  /* Maximum virtual page count */

/* CPU & System limits */
#define MAX_CPU 4
#define MAX_PROCESS 20
#define DEFAULT_TIME_SLICE 2

#endif /* OS_CFG_H */
