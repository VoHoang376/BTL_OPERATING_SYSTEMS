#ifndef CPU_H
#define CPU_H

#include "common.h"

struct cpu_t {
    int id;
    int time_slice;
    struct pcb_t *current_proc;
    pthread_t thread;
    bool running;
};

/* CPU interface */
void *cpu_routine(void *arg);
int run(struct pcb_t *proc);
void check_and_handle_interrupts(struct cpu_t *cpu);

#endif /* CPU_H */
