#ifndef TIMER_H
#define TIMER_H

#include <pthread.h>

/* Timer interface */
void start_timer(void);
void stop_timer(void);
int current_time(void);

#endif /* TIMER_H */
