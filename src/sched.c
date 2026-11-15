#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "sched.h"
#include "common.h"

static struct queue_t mlq_ready_queue[MAX_PRIO];
static pthread_mutex_t queue_lock = PTHREAD_MUTEX_INITIALIZER;

void init_scheduler(void) {
    pthread_mutex_lock(&queue_lock);
    for (int i = 0; i < MAX_PRIO; i++) {
        init_queue(&mlq_ready_queue[i]);
        /* Slot allocated for this priority queue: slot = MAX_PRIO - prio */
        mlq_ready_queue[i].slot = MAX_PRIO - i;
    }
    kernel.mlq_ready_queue = mlq_ready_queue;
    pthread_mutex_unlock(&queue_lock);
}

struct pcb_t *get_proc(void) {
    struct pcb_t *proc = NULL;
    pthread_mutex_lock(&queue_lock);

    /*
     * MLQ Scheduling Algorithm:
     * 1. Tìm hàng đợi ưu tiên cao nhất có tiến trình và còn slot (> 0)
     */
    for (int prio = 0; prio < MAX_PRIO; prio++) {
        if (!empty(&mlq_ready_queue[prio]) && mlq_ready_queue[prio].slot > 0) {
            proc = dequeue(&mlq_ready_queue[prio]);
            mlq_ready_queue[prio].slot--;
            pthread_mutex_unlock(&queue_lock);
            return proc;
        }
    }

    /*
     * 2. Nếu tất cả các hàng đợi có tiến trình đều đã hết slot trong lượt này,
     *    reset lại slot cho tất cả các hàng đợi theo công thức: slot = MAX_PRIO - prio
     */
    int has_any_proc = 0;
    for (int prio = 0; prio < MAX_PRIO; prio++) {
        mlq_ready_queue[prio].slot = MAX_PRIO - prio;
        if (!empty(&mlq_ready_queue[prio])) {
            has_any_proc = 1;
        }
    }

    /* 3. Lấy tiến trình sau khi reset slot */
    if (has_any_proc) {
        for (int prio = 0; prio < MAX_PRIO; prio++) {
            if (!empty(&mlq_ready_queue[prio]) && mlq_ready_queue[prio].slot > 0) {
                proc = dequeue(&mlq_ready_queue[prio]);
                mlq_ready_queue[prio].slot--;
                break;
            }
        }
    }

    pthread_mutex_unlock(&queue_lock);
    return proc;
}

void put_proc(struct pcb_t *proc) {
    if (!proc) return;
    pthread_mutex_lock(&queue_lock);

    uint32_t prio = proc->priority;
#ifdef MLQ_SCHED
    prio = proc->prio;
#endif
    if (prio >= MAX_PRIO) prio = MAX_PRIO - 1;
    enqueue(&mlq_ready_queue[prio], proc);

    pthread_mutex_unlock(&queue_lock);
}

void add_mlq(struct pcb_t *proc) {
    put_proc(proc);
}

static struct pcb_t *procs_table[MAX_PROCESS];
static int num_procs_registered = 0;

void register_pcb(struct pcb_t *proc) {
    if (!proc || num_procs_registered >= MAX_PROCESS) return;
    pthread_mutex_lock(&queue_lock);
    procs_table[num_procs_registered++] = proc;
    pthread_mutex_unlock(&queue_lock);
}

struct pcb_t *get_pcb_by_pid(uint32_t pid) {
    pthread_mutex_lock(&queue_lock);
    for (int i = 0; i < num_procs_registered; i++) {
        if (procs_table[i] && procs_table[i]->pid == pid) {
            struct pcb_t *res = procs_table[i];
            pthread_mutex_unlock(&queue_lock);
            return res;
        }
    }
    pthread_mutex_unlock(&queue_lock);
    return NULL;
}
