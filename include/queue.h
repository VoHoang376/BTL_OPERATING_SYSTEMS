#ifndef QUEUE_H
#define QUEUE_H

#include "common.h"

#define MAX_QUEUE_SIZE 100

struct queue_t {
    struct pcb_t *proc[MAX_QUEUE_SIZE];
    int size;
    int slot; /* Time slot tracker for MLQ scheduling */
};

/* Queue operations */
void init_queue(struct queue_t *q);
void enqueue(struct queue_t *q, struct pcb_t *proc);
struct pcb_t *dequeue(struct queue_t *q);
int empty(struct queue_t *q);

#endif /* QUEUE_H */
