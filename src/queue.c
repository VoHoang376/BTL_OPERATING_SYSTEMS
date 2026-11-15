#include <stdio.h>
#include <stdlib.h>
#include "queue.h"

void init_queue(struct queue_t *q) {
    if (!q) return;
    q->size = 0;
    q->slot = 0;
    for (int i = 0; i < MAX_QUEUE_SIZE; i++) {
        q->proc[i] = NULL;
    }
}

void enqueue(struct queue_t *q, struct pcb_t *proc) {
    /* TODO: TV1 - Implement enqueue operation into priority queue */
    if (!q || !proc) return;
    if (q->size >= MAX_QUEUE_SIZE) {
        fprintf(stderr, "[Queue] Warning: Queue is full, cannot enqueue PID %u\n", proc->pid);
        return;
    }
    q->proc[q->size++] = proc;
}

struct pcb_t *dequeue(struct queue_t *q) {
    /* TODO: TV1 - Implement dequeue operation to fetch the next in-turn process */
    if (!q || q->size == 0) return NULL;
    struct pcb_t *proc = q->proc[0];
    for (int i = 0; i < q->size - 1; i++) {
        q->proc[i] = q->proc[i + 1];
    }
    q->proc[--q->size] = NULL;
    return proc;
}

int empty(struct queue_t *q) {
    if (!q) return 1;
    return (q->size == 0);
}
