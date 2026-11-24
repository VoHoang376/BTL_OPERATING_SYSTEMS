#include <stdio.h>
#include <stdlib.h>
#include "queue.h"

int
empty (struct queue_t *q)
{
  if (q == NULL)
    return 1;
  return (q->size == 0);
}

void
enqueue (struct queue_t *q, struct pcb_t *proc)
{
  if (q == NULL || proc == NULL)
    return;

  if (q->size >= MAX_QUEUE_SIZE)
    return;

  q->proc[q->size] = proc;
  q->size++;
}

struct pcb_t *
dequeue (struct queue_t *q)
{
  if (empty (q))
    return NULL;

  int best_idx = 0;

#ifdef MLQ_SCHED
  uint32_t best_prio = q->proc[0]->prio;
  for (int i = 1; i < q->size; i++)
    {
      if (q->proc[i]->prio < best_prio)
        {
          best_prio = q->proc[i]->prio;
          best_idx = i;
        }
    }
#else
  uint32_t best_prio = q->proc[0]->priority;
  for (int i = 1; i < q->size; i++)
    {
      if (q->proc[i]->priority < best_prio)
        {
          best_prio = q->proc[i]->priority;
          best_idx = i;
        }
    }
#endif

  struct pcb_t *proc = q->proc[best_idx];

  for (int i = best_idx; i < q->size - 1; i++)
    {
      q->proc[i] = q->proc[i + 1];
    }

  q->size--;
  q->proc[q->size] = NULL;

  return proc;
}

struct pcb_t *
purgequeue (struct queue_t *q, struct pcb_t *proc)
{
  if (empty (q) || proc == NULL)
    return NULL;

  int found_idx = -1;
  for (int i = 0; i < q->size; i++)
    {
      if (q->proc[i] == proc)
        {
          found_idx = i;
          break;
        }
    }

  if (found_idx == -1)
    return NULL;

  struct pcb_t *removed = q->proc[found_idx];

  for (int i = found_idx; i < q->size - 1; i++)
    {
      q->proc[i] = q->proc[i + 1];
    }

  q->size--;
  q->proc[q->size] = NULL;

  return removed;
}