#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <stdbool.h>
#include "timer.h"

static int system_time = 0;
static bool timer_running = false;
static pthread_t timer_thread;
static pthread_mutex_t timer_lock = PTHREAD_MUTEX_INITIALIZER;

static void *timer_routine(void *arg) {
    (void)arg;
    while (timer_running) {
        usleep(100000); /* 100ms per tick or time unit */
        pthread_mutex_lock(&timer_lock);
        system_time++;
        pthread_mutex_unlock(&timer_lock);
    }
    return NULL;
}

void start_timer(void) {
    timer_running = true;
    pthread_create(&timer_thread, NULL, timer_routine, NULL);
}

void stop_timer(void) {
    timer_running = false;
    pthread_join(timer_thread, NULL);
}

int current_time(void) {
    int t;
    pthread_mutex_lock(&timer_lock);
    t = system_time;
    pthread_mutex_unlock(&timer_lock);
    return t;
}
