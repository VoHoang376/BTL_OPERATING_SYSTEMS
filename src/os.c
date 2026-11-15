#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include "common.h"
#include "sched.h"
#include "cpu.h"
#include "timer.h"
#include "loader.h"
#include "mm.h"
#include "dma.h"

struct krnl_t kernel;

struct proc_entry {
    int arrival_time;
    char path[100];
    uint32_t priority;
    bool loaded;
};

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <configure_file>\n", argv[0]);
        printf("Example: %s input/os_default\n", argv[0]);
        return 1;
    }

    const char *cfg_path = argv[1];
    FILE *fp = fopen(cfg_path, "r");
    if (!fp) {
        fprintf(stderr, "Error: Cannot open configure file %s\n", cfg_path);
        return 1;
    }

    int time_slice = DEFAULT_TIME_SLICE;
    int num_cpu = 1;
    int num_proc = 0;

    if (fscanf(fp, "%d %d %d", &time_slice, &num_cpu, &num_proc) != 3) {
        fprintf(stderr, "Invalid configuration header in %s\n", cfg_path);
        fclose(fp);
        return 1;
    }

    printf("=== Simple Operating System Simulation Booting ===\n");
    printf("Time slice: %d | CPUs: %d | Total processes: %d\n", time_slice, num_cpu, num_proc);

    /* Initialize Kernel Global Structure */
    memset(&kernel, 0, sizeof(struct krnl_t));
    pthread_mutex_init(&kernel.kernel_lock, NULL);

    /* Initialize Memory Subsystem (RAM and SWAP devices) */
    kernel.mram = (struct memphy_struct *)malloc(sizeof(struct memphy_struct));
    init_memphy(kernel.mram, 1024 * 1024, 1); /* 1MB default RAM */

    /* Nạp dữ liệu mẫu ban đầu tại địa chỉ bus 0 để trực quan hóa DMA copy */
    if (kernel.mram && kernel.mram->storage) {
        kernel.mram->storage[0] = 0xAA;
        kernel.mram->storage[1] = 0xBB;
        kernel.mram->storage[2] = 0xCC;
        kernel.mram->storage[3] = 0xDD;
    }

    kernel.mswp = (struct memphy_struct **)malloc(sizeof(struct memphy_struct *) * PAGING_MAX_MMSWP);
    for (int i = 0; i < PAGING_MAX_MMSWP; i++) {
        kernel.mswp[i] = (struct memphy_struct *)malloc(sizeof(struct memphy_struct));
        init_memphy(kernel.mswp[i], 4 * 1024 * 1024, 0); /* 4MB SWAP each */
    }
    kernel.active_mswp = kernel.mswp[0];
    kernel.active_mswp_id = 0;

    /* Initialize DMA Co-processor */
    dma_init();

    /* Initialize MLQ Scheduler */
    init_scheduler();

    /* Read process list from configuration file */
    struct proc_entry *proc_list = (struct proc_entry *)malloc(sizeof(struct proc_entry) * num_proc);
    for (int i = 0; i < num_proc; i++) {
        fscanf(fp, "%d %s %u", &proc_list[i].arrival_time, proc_list[i].path, &proc_list[i].priority);
        proc_list[i].loaded = false;
    }
    fclose(fp);

    /* Start System Timer */
    start_timer();

    /* Initialize and start virtual CPUs */
    struct cpu_t *cpus = (struct cpu_t *)malloc(sizeof(struct cpu_t) * num_cpu);
    for (int i = 0; i < num_cpu; i++) {
        cpus[i].id = i;
        cpus[i].time_slice = time_slice;
        cpus[i].running = true;
        cpus[i].current_proc = NULL;
        pthread_create(&cpus[i].thread, NULL, cpu_routine, &cpus[i]);
    }

    /* Simulation dispatcher loop */
    int active_procs = num_proc;
    while (active_procs > 0) {
        int t = current_time();
        for (int i = 0; i < num_proc; i++) {
            if (!proc_list[i].loaded && proc_list[i].arrival_time <= t) {
                printf("[OS] Time %d: Loading process %s with priority %u\n", t, proc_list[i].path, proc_list[i].priority);
                struct pcb_t *proc = load(proc_list[i].path);
                if (proc) {
                    /* Dual priority override: configuration priority overwrites default */
                    proc->priority = proc_list[i].priority;
#ifdef MLQ_SCHED
                    proc->prio = proc_list[i].priority;
#endif
                    /* Initialize process memory structure */
                    proc->mm = (struct mm_struct *)malloc(sizeof(struct mm_struct));
                    init_mm(proc->mm, proc);

                    register_pcb(proc);
                    add_mlq(proc);
                }
                proc_list[i].loaded = true;
            }
        }
        usleep(100000);
        /* Check termination condition (simple timeout/break for framework demo) */
        if (t >= 20) {
            printf("[OS] Simulation duration reached.\n");
            break;
        }
    }

    /* Stop CPUs */
    for (int i = 0; i < num_cpu; i++) {
        cpus[i].running = false;
        pthread_join(cpus[i].thread, NULL);
        printf("[OS] CPU %d stopped.\n", i);
    }

    /* Stop Timer & DMA */
    stop_timer();
    dma_stop();
    printf("=== Simple Operating System Simulation Finished ===\n");

    /* Cleanup */
    free(proc_list);
    free(cpus);
    return 0;
}
