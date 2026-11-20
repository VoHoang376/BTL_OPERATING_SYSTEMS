/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 *
 * demacOprx release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 *
 * System OS module
 */
#include "cpu.h"
#include "timer.h"
#include "sched.h"
#include "loader.h"
#include "mm.h"
#ifdef MM64
#include "mm64.h"
#endif

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>

#include "sysbus.h"
#include "dpi_rtl.h"

static int time_slot;
static int num_cpus;
static int done = 0;
static struct krnl_t os;

#ifdef MM_PAGING
static uint64_t memramsz;
static uint64_t memswpsz[PAGING_MAX_MMSWP];

struct mmpaging_ld_args {
	struct memphy_struct *mram;
	struct memphy_struct **mswp;
	struct memphy_struct *active_mswp;
	struct timer_id_t  *timer_id;
};
#endif

static struct ld_args{
	char ** path;
	unsigned long * start_time;
#ifdef MLQ_SCHED
	unsigned long * prio;
#endif
} ld_processes;
int num_processes;

struct cpu_args {
	struct timer_id_t * timer_id;
	int id;
};

static void * cpu_routine(void * args) {
	struct timer_id_t * timer_id = ((struct cpu_args*)args)->timer_id;
	int id = ((struct cpu_args*)args)->id;
	/* Check for new process in ready queue */
	int time_left = 0;
	struct pcb_t * proc = NULL;
	while (1) {
		/* Check the status of current process */
		if (proc == NULL) {
			/* No process is running, the we load new process from
		 	* ready queue */
			proc = get_proc();
			if (proc == NULL) {
                           next_slot(timer_id);
                           continue; /* First load failed. skip dummy load */
                        }
		}else if (proc->pc == proc->code->size) {
			printf("\tCPU %d: Processed %2d has finished\n",
				id ,proc->pid);
			remove_running_proc(proc);
#ifdef MM_PAGING
#ifdef MM64
			print_mmu_stats(proc);
#endif
			if (free_pcb_memph(proc) != 0) {
				fprintf(stderr, "Cannot release memory for PID %u\n", proc->pid);
				exit(EXIT_FAILURE);
			}
			free(proc->mm);
#endif
			free(proc->code->text);
			free(proc->code);
			free(proc->page_table);
			free(proc);
			proc = get_proc();
			time_left = 0;
		}else if (time_left == 0) {
			/* The process has done its job in current time slot */
			printf("\tCPU %d: Put process %2d to run queue\n",
				id, proc->pid);
			put_proc(proc);
			proc = get_proc();
		}
		
		/* Recheck process status after loading new process */
		if (proc == NULL && done) {
			/* No process to run, exit */
			printf("\tCPU %d stopped\n", id);
			break;
		}else if (proc == NULL) {
			/* There may be new processes to run in
			 * next time slots, just skip current slot */
			next_slot(timer_id);
			continue;
		}else if (time_left == 0) {
			printf("\tCPU %d: Dispatched process %2d\n",
				id, proc->pid);
			time_left = time_slot;
		}
		
		/* Run current process */
		run(proc);
		time_left--;
		next_slot(timer_id);
	}
	detach_event(timer_id);
	pthread_exit(NULL);
}


static void * co_processor(void *arg) {
    (void)arg;
    /* TODO: Initialize the internal Verilator RTL simulation.
	 *       This is necessary to ensure that the RTL simulation is ready for DPI calls.
      */
    if (rtl_dma_init_simulation() != 0) {
        printf("Cannot initialize RTL simulation\n");
        return (void*)1;
    }
	
	/* TODO: This asynchronous implementation of DMA
     *        DMA completion callback is not yet implemented in this testbench
     */
    // Install callback handler
    //dma_done_callback();  // Call the callback to signal DMA completion
    return NULL;
}

static void * ld_routine(void * args) {
  printf("ld_routine\n");
#ifdef MM_PAGING
  const struct mmpaging_ld_args *paging = args;
  struct timer_id_t *timer_id = paging->timer_id;
  os.mram = paging->mram;
  os.mswp = paging->mswp;
  os.active_mswp = paging->active_mswp;
  os.mm = malloc(sizeof(*os.mm));
#ifdef MM64
  if (init_mm(os.mm, NULL) != 0)
  {
    fprintf(stderr, "Cannot initialize kernel memory\n");
    exit(EXIT_FAILURE);
  }
  os.krnl_pgd = os.mm->pgd;
#else
  os.krnl_pgd = malloc(PAGING_MAX_PGN * sizeof(*os.krnl_pgd));
  init_mm(os.mm, NULL);
#endif
#else
  struct timer_id_t *timer_id = args;
#endif

  for (int i = 0; i < num_processes; i++)
  {
    struct pcb_t *proc = load(ld_processes.path[i]);
    proc->krnl = &os;

#ifdef MLQ_SCHED
    proc->prio = ld_processes.prio[i];
#endif

    while (current_time() < ld_processes.start_time[i])
      next_slot(timer_id);

#ifdef MM_PAGING
    proc->mm = malloc(sizeof(*proc->mm));
    if (init_mm(proc->mm, proc) != 0)
    {
      fprintf(stderr, "Cannot initialize memory for PID %u\n", proc->pid);
      exit(EXIT_FAILURE);
    }
#endif

    printf("\tLoaded a process at %s, PID: %u PRIO: %u\n",
           ld_processes.path[i], proc->pid, proc->prio);
    add_proc(proc);
    free(ld_processes.path[i]);
    next_slot(timer_id);
  }

  free(ld_processes.path);
  free(ld_processes.start_time);
  done = 1;
  detach_event(timer_id);
  pthread_exit(NULL);
}

static void read_config(const char * path) {
	FILE * file;
	if ((file = fopen(path, "r")) == NULL) {
		printf("Cannot find configure file at %s\n", path);
		exit(1);
	}
	fscanf(file, "%d %d %d\n", &time_slot, &num_cpus, &num_processes);
	ld_processes.path = (char**)malloc(sizeof(char*) * num_processes);
	ld_processes.start_time = (unsigned long*)
		malloc(sizeof(unsigned long) * num_processes);
#ifdef MM_PAGING
	int sit;
#ifdef MM_FIXED_MEMSZ
	/* We provide here a back compatible with legacy OS simulatiom config file
         * In which, it have no addition config line for Mema, keep only one line
	 * for legacy info 
         *  [time slice] [N = Number of CPU] [M = Number of Processes to be run]
         */
#ifdef MM64
	/* The PTE can address 8192 physical frames. */
	memramsz = ((uint64_t)PAGING_PTE_FPN_MASK + 1) * PAGING64_PAGESZ;
#else
	memramsz = PAGING_MEMRAMSZ;
#endif
        memswpsz[0] = 0x1000000;
	for(sit = 1; sit < PAGING_MAX_MMSWP; sit++)
		memswpsz[sit] = 0;
#else
	/* Read input config of memory size: MEMRAM and upto 4 MEMSWP (mem swap)
	 * Format: (size=0 result non-used memswap, must have RAM and at least 1 SWAP)
	 *        MEM_RAM_SZ MEM_SWP0_SZ MEM_SWP1_SZ MEM_SWP2_SZ MEM_SWP3_SZ
	*/
	if (fscanf(file, "%" SCNu64, &memramsz) != 1 || memramsz == 0 ||
	    memramsz > INT_MAX) {
		fprintf(stderr, "Invalid RAM size in %s (expected 1 to %d bytes)\n", path, INT_MAX);
		exit(EXIT_FAILURE);
	}
	for (sit = 0; sit < PAGING_MAX_MMSWP; sit++) {
		if (fscanf(file, "%" SCNu64, &memswpsz[sit]) != 1 || memswpsz[sit] > INT_MAX) {
			fprintf(stderr, "Invalid SWAP[%d] size in %s (expected 0 to %d bytes)\n",
			        sit, path, INT_MAX);
			exit(EXIT_FAILURE);
		}
	}

       fscanf(file, "\n"); /* Final character */
#endif
#endif

#ifdef MLQ_SCHED
	ld_processes.prio = (unsigned long*)
		malloc(sizeof(unsigned long) * num_processes);
#endif
	int i;
	for (i = 0; i < num_processes; i++) {
		ld_processes.path[i] = (char*)malloc(sizeof(char) * 100);
		ld_processes.path[i][0] = '\0';
		strcat(ld_processes.path[i], "input/proc/");
		char proc[100];
#ifdef MLQ_SCHED
		fscanf(file, "%lu %s %lu\n", &ld_processes.start_time[i], proc, &ld_processes.prio[i]);
#else
		fscanf(file, "%lu %s\n", &ld_processes.start_time[i], proc);
#endif
		strcat(ld_processes.path[i], proc);
	}
}

int main(int argc, char * argv[]) {
	/* Read config */
	if (argc != 2) {
		printf("Usage: os [path to configure file]\n");
		return 1;
	}
	char path[100];
	path[0] = '\0';
	strcat(path, "input/");
	strcat(path, argv[1]);
	read_config(path);

	pthread_t * cpu = (pthread_t*)malloc(num_cpus * sizeof(pthread_t));
	pthread_t * dma = (pthread_t*)malloc(sizeof(pthread_t));
	struct cpu_args * args =
		(struct cpu_args*)malloc(sizeof(struct cpu_args) * num_cpus);
	pthread_t ld;
	
	/* Init timer */
	int i;
	for (i = 0; i < num_cpus; i++) {
		args[i].timer_id = attach_event();
		args[i].id = i;
	}
	struct timer_id_t * ld_event = attach_event();

#ifdef MM_PAGING
	/* Init all MEMPHY include 1 MEMRAM and n of MEMSWP */
	int rdmflag = 1; /* By default memphy is RANDOM ACCESS MEMORY */

	struct memphy_struct mram;
	struct memphy_struct mswp[PAGING_MAX_MMSWP];
	struct memphy_struct *swap_devices[PAGING_MAX_MMSWP];

	/* Create MEM RAM */
	if (init_memphy(&mram, memramsz, rdmflag) != 0) {
		fprintf(stderr, "Cannot initialize RAM (%" PRIu64 " bytes)\n", memramsz);
		return EXIT_FAILURE;
	}

        /* Create all MEM SWAP */ 
	int sit;
	for (sit = 0; sit < PAGING_MAX_MMSWP; sit++) {
		if (init_memphy(&mswp[sit], memswpsz[sit], rdmflag) != 0) {
			fprintf(stderr, "Cannot initialize SWAP[%d] (%" PRIu64 " bytes)\n",
			        sit, memswpsz[sit]);
			return EXIT_FAILURE;
		}
		swap_devices[sit] = &mswp[sit];
	}

	struct mmpaging_ld_args mm_ld_args = {
		.mram = &mram,
		.mswp = swap_devices,
		.active_mswp = &mswp[0],
		.timer_id = ld_event,
	};
#endif

	/* Init scheduler */
	start_timer();
	init_scheduler();

	/* Run CPU and loader */
#ifdef MM_PAGING
	pthread_create(&ld, NULL, ld_routine, &mm_ld_args);
#else
	pthread_create(&ld, NULL, ld_routine, (void*)ld_event);
#endif
	for (i = 0; i < num_cpus; i++) {
		pthread_create(&cpu[i], NULL,
			cpu_routine, (void*)&args[i]);
	}

	/* Run co-processor */
	pthread_create(dma, NULL, co_processor, NULL);

	/* Wait for CPU and loader finishing */
	for (i = 0; i < num_cpus; i++) {
		pthread_join(cpu[i], NULL);
	}
	pthread_join(ld, NULL);

	/* Stop timer */
	stop_timer();

	return 0;

}



