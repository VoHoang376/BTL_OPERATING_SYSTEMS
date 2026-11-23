/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* demacOprx release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

#include "queue.h"
#include "sched.h"
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>

static struct queue_t ready_queue;
static struct queue_t run_queue;
static pthread_mutex_t queue_lock;

static struct queue_t running_list;

#ifdef MLQ_SCHED
static struct queue_t mlq_ready_queue[MAX_PRIO];
static int slot[MAX_PRIO];
#endif

int
queue_empty (void)
{
  int res = 1;

  pthread_mutex_lock (&queue_lock);
#ifdef MLQ_SCHED
  for (unsigned long prio = 0; prio < MAX_PRIO; prio++)
    {
      if (!empty (&mlq_ready_queue[prio]))
        {
          res = -1;
          break;
        }
    }
  if (res != -1)
    res = (empty (&ready_queue) && empty (&run_queue));
#else
  res = (empty (&ready_queue) && empty (&run_queue));
#endif
  pthread_mutex_unlock (&queue_lock);

  return res;
}

void
init_scheduler (void)
{
#ifdef MLQ_SCHED
  for (int i = 0; i < MAX_PRIO; i++)
    {
      mlq_ready_queue[i].size = 0;
      slot[i] = MAX_PRIO - i;
    }
#endif
  ready_queue.size = 0;
  run_queue.size = 0;
  running_list.size = 0;
  pthread_mutex_init (&queue_lock, NULL);
}

void
finish_scheduler (void)
{
  pthread_mutex_destroy (&queue_lock);
}

#ifdef MLQ_SCHED
/*
 * Stateful design for routine calling
 * based on the priority and our MLQ policy.
 * State representation: prio = 0 .. (MAX_PRIO - 1), slot[prio] = 0 .. (MAX_PRIO - prio)
 */
struct pcb_t *
get_mlq_proc (void)
{
  struct pcb_t *proc = NULL;

  pthread_mutex_lock (&queue_lock);

  /* Pass 1: chon hang doi uu tien cao nhat con tien trinh va con slot */
  for (int prio = 0; prio < MAX_PRIO; prio++)
    {
      if (!empty (&mlq_ready_queue[prio]) && slot[prio] > 0)
        {
          proc = dequeue (&mlq_ready_queue[prio]);
          if (proc != NULL)
            {
              slot[prio]--;
              break;
            }
        }
    }

  /* Pass 2: neu chua lay duoc nhung van con tien trinh dang cho -> reset chu ky slot */
  if (proc == NULL)
    {
      int has_pending = 0;
      for (int prio = 0; prio < MAX_PRIO; prio++)
        {
          if (!empty (&mlq_ready_queue[prio]))
            {
              has_pending = 1;
              break;
            }
        }

      if (has_pending)
        {
          for (int prio = 0; prio < MAX_PRIO; prio++)
            {
              slot[prio] = MAX_PRIO - prio;
            }

          for (int prio = 0; prio < MAX_PRIO; prio++)
            {
              if (!empty (&mlq_ready_queue[prio]) && slot[prio] > 0)
                {
                  proc = dequeue (&mlq_ready_queue[prio]);
                  if (proc != NULL)
                    {
                      slot[prio]--;
                      break;
                    }
                }
            }
        }
    }

  if (proc != NULL)
    enqueue (&running_list, proc);

  pthread_mutex_unlock (&queue_lock);

  return proc;
}

void
put_mlq_proc (struct pcb_t *proc)
{
  if (proc == NULL || proc->prio >= MAX_PRIO)
    return;

  pthread_mutex_lock (&queue_lock);

  if (proc->krnl != NULL)
    {
      proc->krnl->ready_queue = &ready_queue;
      proc->krnl->mlq_ready_queue = mlq_ready_queue;
      proc->krnl->running_list = &running_list;
    }

  purgequeue (&running_list, proc);
  enqueue (&mlq_ready_queue[proc->prio], proc);

  pthread_mutex_unlock (&queue_lock);
}

void
add_mlq_proc (struct pcb_t *proc)
{
  if (proc == NULL || proc->prio >= MAX_PRIO)
    return;

  pthread_mutex_lock (&queue_lock);

  if (proc->krnl != NULL)
    {
      proc->krnl->ready_queue = &ready_queue;
      proc->krnl->mlq_ready_queue = mlq_ready_queue;
      proc->krnl->running_list = &running_list;
    }

  enqueue (&mlq_ready_queue[proc->prio], proc);

  pthread_mutex_unlock (&queue_lock);
}

struct pcb_t *
get_proc (void)
{
  return get_mlq_proc ();
}

void
put_proc (struct pcb_t *proc)
{
  put_mlq_proc (proc);
}

void
add_proc (struct pcb_t *proc)
{
  add_mlq_proc (proc);
}
#else
struct pcb_t *
get_proc (void)
{
  struct pcb_t *proc = NULL;

  pthread_mutex_lock (&queue_lock);

  if (empty (&ready_queue))
    {
      while (!empty (&run_queue))
        {
          enqueue (&ready_queue, dequeue (&run_queue));
        }
    }

  proc = dequeue (&ready_queue);

  if (proc != NULL)
    enqueue (&running_list, proc);

  pthread_mutex_unlock (&queue_lock);

  return proc;
}

void
put_proc (struct pcb_t *proc)
{
  if (proc == NULL)
    return;

  pthread_mutex_lock (&queue_lock);

  if (proc->krnl != NULL)
    {
      proc->krnl->ready_queue = &ready_queue;
      proc->krnl->running_list = &running_list;
    }

  purgequeue (&running_list, proc);
  enqueue (&run_queue, proc);

  pthread_mutex_unlock (&queue_lock);
}

void
add_proc (struct pcb_t *proc)
{
  if (proc == NULL)
    return;

  pthread_mutex_lock (&queue_lock);

  if (proc->krnl != NULL)
    {
      proc->krnl->ready_queue = &ready_queue;
      proc->krnl->running_list = &running_list;
    }

  enqueue (&ready_queue, proc);

  pthread_mutex_unlock (&queue_lock);
}
#endif