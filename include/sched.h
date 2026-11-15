#ifndef SCHED_H
#define SCHED_H

#include "common.h"
#include "queue.h"

/* Scheduler interface */
void init_scheduler(void);
struct pcb_t *get_proc(void);
void put_proc(struct pcb_t *proc);
void add_mlq(struct pcb_t *proc);
void register_pcb(struct pcb_t *proc);
struct pcb_t *get_pcb_by_pid(uint32_t pid);

#endif /* SCHED_H */
