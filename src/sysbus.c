/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 *
 * demacOprx release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 *
 * system bus - Only for dma_testbech simulation access
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <stdatomic.h>
#include "sysbus.h"

struct pcb_t;   /* opaque here: do_dma() only gets NULL from the worker */

// Maximum number of memory banks supported
#define MAX_MEMORY_BANKS 8

// Memory bank structure
typedef struct {
    uint8_t* data;
    size_t size;
    int initialized;
} memory_bank_t_struct;

// Array of memory banks
static memory_bank_t_struct g_banks[MAX_MEMORY_BANKS] = {0};
static int g_bank_count = 0;

/* OS RAM, used when ./os runs and the testbench bank is not created */
static uint8_t *g_os_ram = NULL;
static size_t g_os_ram_size = 0;

void memory_bus_use_os_ram(void *storage, size_t size) {
    g_os_ram = (uint8_t *)storage;
    g_os_ram_size = size;
}

void *memory_bus_os_ram_ptr(uint64_t offset, uint64_t len) {
    if (g_os_ram == NULL || offset > g_os_ram_size ||
        len > g_os_ram_size - offset) {
        return NULL;
    }
    return g_os_ram + offset;
}

/* ------------------------------------------------------------------ *
 *  Interrupt-driven DMA (slide 22: I/O / co-processor)                *
 *                                                                     *
 *  step 1: CPU thread -> dma_submit()  : writes a command, returns.   *
 *  step 2/3: co-processor thread runs the chip, CPU keeps running.    *
 *  step 4: chip finishes -> dma_done_callback() -> dma_raise_irq()    *
 *          -> SIGRTMIN delivered to the CPU thread that asked ->      *
 *          dma_irq_handler() (the interrupt handler) runs on that CPU.*
 *                                                                     *
 *  Nobody ever waits for a transfer to finish. The mutex below only   *
 *  protects the command queue (like a bus lock), it never blocks a    *
 *  CPU until completion.                                              *
 * ------------------------------------------------------------------ */
#define DMA_IRQ_SIGNAL   SIGRTMIN     /* real-time: queued, never coalesced */
#define DMA_MAX_CPUS     64
#define DMA_PID_SLOTS    256

/* do_dma() runs ONE copy on the RTL chip (defined in dma_c_testbench.cpp). */
extern int do_dma(struct pcb_t *caller, uint64_t source,
                  uint64_t destination, uint64_t length);

struct dma_req {
    uint64_t src, dst, len;
    uint32_t pid;                 /* who asked (for the handler)            */
    int      cpu;                 /* CPU index to interrupt, -1 = none      */
    struct dma_req *next;
};

/* per-CPU "interrupt controller" state: all touched from a signal handler,
 * so only lock-free atomics. */
static struct {
    pthread_t         tid;
    atomic_int        alive;      /* 1 while the CPU thread can take IRQs   */
    atomic_ulong      raised;     /* IRQs sent to it                        */
    atomic_ulong      handled;    /* IRQs its handler has executed          */
} g_cpu[DMA_MAX_CPUS];
static pthread_mutex_t g_cpu_lock = PTHREAD_MUTEX_INITIALIZER; /* raise vs unregister */

static __thread int tls_cpu = -1;  /* CPU index of the calling thread        */

static atomic_ulong g_irq_total;           /* all handlers run               */
static atomic_ulong g_irq_err;             /* handlers that saw an error     */
static atomic_ulong g_pid_irqs[DMA_PID_SLOTS];

static pthread_mutex_t g_q_lock = PTHREAD_MUTEX_INITIALIZER;   /* queue only */
static pthread_cond_t  g_q_cond = PTHREAD_COND_INITIALIZER;    /* new req / shutdown */
static struct dma_req *g_q_head = NULL, *g_q_tail = NULL;
static int             g_shutdown = 0;
static atomic_ulong    g_submitted, g_completed, g_failed;

/* request the chip is running right now (read by the chip callback) */
static struct dma_req *volatile g_cur = NULL;
static atomic_int g_cb_fired;

/* ---- the interrupt handler (async-signal-safe: atomics + write(2)) ---- */
static void put_num(char *b, int *n, unsigned long v) {
    char t[24]; int k = 0;
    if (v == 0) t[k++] = '0';
    while (v) { t[k++] = (char)('0' + v % 10); v /= 10; }
    while (k) b[(*n)++] = t[--k];
}

static void dma_isr_body(int cpu, uint32_t pid, int err) {
    atomic_fetch_add(&g_pid_irqs[pid % DMA_PID_SLOTS], 1);
    if (err) atomic_fetch_add(&g_irq_err, 1);
    atomic_fetch_add(&g_irq_total, 1);
    if (cpu >= 0 && cpu < DMA_MAX_CPUS)
        atomic_fetch_add(&g_cpu[cpu].handled, 1);

    char b[96]; int n = 0;
    const char *s = "[IRQ] DMA done -> handler on CPU ";
    while (*s) b[n++] = *s++;
    if (cpu < 0) b[n++] = '-'; else put_num(b, &n, (unsigned long)cpu);
    s = " for pid ";
    while (*s) b[n++] = *s++;
    put_num(b, &n, pid);
    if (err) { s = " (ERROR)"; while (*s) b[n++] = *s++; }
    b[n++] = '\n';
    if (write(1, b, (size_t)n) < 0) { /* nothing safe to do */ }
}

static void dma_irq_handler(int sig, siginfo_t *si, void *uc) {
    (void)sig; (void)uc;
    int v = si->si_value.sival_int;
    dma_isr_body(tls_cpu, (uint32_t)v >> 1, v & 1);
}

void dma_irq_install(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = dma_irq_handler;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(DMA_IRQ_SIGNAL, &sa, NULL);
    /* Block it here: every thread created afterwards inherits "blocked", so
     * only threads that call dma_register_cpu() can ever take the interrupt. */
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, DMA_IRQ_SIGNAL);
    pthread_sigmask(SIG_BLOCK, &set, NULL);
}

void dma_register_cpu(int cpu_id) {
    if (cpu_id < 0 || cpu_id >= DMA_MAX_CPUS) return;
    tls_cpu = cpu_id;
    pthread_mutex_lock(&g_cpu_lock);
    g_cpu[cpu_id].tid = pthread_self();
    atomic_store(&g_cpu[cpu_id].alive, 1);
    pthread_mutex_unlock(&g_cpu_lock);
    sigset_t set;                         /* this CPU accepts DMA interrupts */
    sigemptyset(&set);
    sigaddset(&set, DMA_IRQ_SIGNAL);
    pthread_sigmask(SIG_UNBLOCK, &set, NULL);
}

void dma_unregister_cpu(void) {
    int c = tls_cpu;
    if (c < 0) return;
    pthread_mutex_lock(&g_cpu_lock);
    atomic_store(&g_cpu[c].alive, 0);     /* no new IRQ will be sent here  */
    pthread_mutex_unlock(&g_cpu_lock);
    /* IRQs already sent must still run on this CPU before it goes away
     * (signal stays unblocked, so the handler runs while we wait). */
    for (int i = 0; i < 2000 &&
         atomic_load(&g_cpu[c].handled) < atomic_load(&g_cpu[c].raised); i++)
        usleep(100);
    tls_cpu = -1;
}

/* Chip side: send the interrupt line to the CPU that issued the command. */
static void dma_raise_irq(struct dma_req *r, int err) {
    uint32_t v = ((r->pid & 0x7fffffffu) << 1) | (uint32_t)(err & 1);
    union sigval sv; sv.sival_int = (int)v;
    int sent = 0;
    if (r->cpu >= 0 && r->cpu < DMA_MAX_CPUS) {
        pthread_mutex_lock(&g_cpu_lock);
        if (atomic_load(&g_cpu[r->cpu].alive)) {
            atomic_fetch_add(&g_cpu[r->cpu].raised, 1);
            if (pthread_sigqueue(g_cpu[r->cpu].tid, DMA_IRQ_SIGNAL, sv) == 0)
                sent = 1;
            else
                atomic_fetch_add(&g_cpu[r->cpu].handled, 1); /* keep counters balanced */
        }
        pthread_mutex_unlock(&g_cpu_lock);
    }
    if (!sent)  /* requester CPU is gone: run the handler on the co-processor */
        dma_isr_body(-1, r->pid, err);
}

int dma_submit(uint32_t pid, uint64_t src_pa, uint64_t dst_pa, uint64_t len) {
    if (len == 0)
        return 0;
    if (len % 4 != 0)               /* the chip moves whole 32-bit words */
        return -1;

    struct dma_req *r = (struct dma_req *)malloc(sizeof(*r));
    if (r == NULL)
        return -1;
    r->src = src_pa; r->dst = dst_pa; r->len = len; r->next = NULL;
    r->pid = pid; r->cpu = tls_cpu;

    pthread_mutex_lock(&g_q_lock);
    if (g_shutdown) {
        pthread_mutex_unlock(&g_q_lock);
        free(r);
        return -1;
    }
    if (g_q_tail) g_q_tail->next = r; else g_q_head = r;
    g_q_tail = r;
    atomic_fetch_add(&g_submitted, 1);
    pthread_cond_signal(&g_q_cond);
    pthread_mutex_unlock(&g_q_lock);
    return 0;                       /* "command written": CPU carries on */
}

void dma_worker_loop(void) {
    for (;;) {
        pthread_mutex_lock(&g_q_lock);
        while (g_q_head == NULL && !g_shutdown)
            pthread_cond_wait(&g_q_cond, &g_q_lock);
        if (g_q_head == NULL) {     /* shutdown requested and queue drained */
            pthread_mutex_unlock(&g_q_lock);
            return;
        }
        struct dma_req *r = g_q_head;
        g_q_head = r->next;
        if (g_q_head == NULL) g_q_tail = NULL;
        pthread_mutex_unlock(&g_q_lock);

        /* The chip raises ch_done -> dma_done_callback() -> interrupt. The
         * request only holds physical offsets and a CPU index, never a pcb_t
         * pointer, so the process may already be gone. */
        atomic_store(&g_cb_fired, 0);
        g_cur = r;
        int rc = do_dma(NULL, r->src, r->dst, r->len);
        g_cur = NULL;

        if (rc == 0) atomic_fetch_add(&g_completed, 1);
        else         atomic_fetch_add(&g_failed, 1);
        if (rc != 0 && !atomic_load(&g_cb_fired))
            dma_raise_irq(r, 1);    /* chip never finished: error interrupt */
        free(r);
    }
}

/* Used only by main() at shutdown (never by a CPU): wait until every
 * interrupt has been handled. Polling, no lock, no condition variable. */
void dma_wait_idle(void) {
    for (;;) {
        unsigned long done = atomic_load(&g_completed) + atomic_load(&g_failed);
        if (done >= atomic_load(&g_submitted) &&
            atomic_load(&g_irq_total) >= done)
            return;
        usleep(200);
    }
}

void dma_queue_shutdown(void) {
    pthread_mutex_lock(&g_q_lock);
    g_shutdown = 1;
    pthread_cond_broadcast(&g_q_cond);
    pthread_mutex_unlock(&g_q_lock);
}

unsigned long dma_completed_count(void) { return atomic_load(&g_completed); }
unsigned long dma_failed_count(void)    { return atomic_load(&g_failed); }
unsigned long dma_irq_count(void)       { return atomic_load(&g_irq_total); }
unsigned long dma_pid_irq_count(uint32_t pid) {
    return atomic_load(&g_pid_irqs[pid % DMA_PID_SLOTS]);
}

// Initialize a memory bank with given size in bytes
memory_bank_t memory_bank_init(size_t size_bytes) {
    // Find first free slot
    int free_slot = -1;
    for (int i = 0; i < MAX_MEMORY_BANKS; i++) {
        if (!g_banks[i].initialized) {
            free_slot = i;
            break;
        }
    }

    if (free_slot == -1) {
        printf("Maximum number of memory banks (%d) reached\n", MAX_MEMORY_BANKS);
        return MEMORY_BANK_INVALID;
    }

    if (size_bytes == 0) {
        printf("Invalid bank size: 0\n");
        return MEMORY_BANK_INVALID;
    }

    uint8_t* data = (uint8_t*)malloc(size_bytes);
    if (!data) {
        printf("Failed to allocate memory bank of %zu bytes\n", size_bytes);
        return MEMORY_BANK_INVALID;
    }

    memset(data, 0, size_bytes);
    g_banks[free_slot].data = data;
    g_banks[free_slot].size = size_bytes;
    g_banks[free_slot].initialized = 1;
    g_bank_count++;

    printf("Memory bank [%d] initialized: %zu bytes at address %p\n", free_slot, size_bytes, (void*)data);
    return free_slot;
}

// Cleanup memory bank
void memory_bank_cleanup(void) {
    for (int i = 0; i < MAX_MEMORY_BANKS; i++) {
        if (g_banks[i].initialized) {
            if (g_banks[i].data) {
                free(g_banks[i].data);
                g_banks[i].data = NULL;
            }
            g_banks[i].size = 0;
            g_banks[i].initialized = 0;
            g_bank_count--;
            printf("Memory bank [%d] cleaned up\n", i);
        }
    }
}

int memory_bank_is_available(void) {
    // Check if at least one memory bank is initialized
    for (int i = 0; i < MAX_MEMORY_BANKS; i++) {
        if (g_banks[i].initialized) {
            return 1;
        }
    }
    return 0;
}

// Get pointer to locate data at offset
void* memory_bank_get_ptr(uint64_t offset) {
    if (offset >= g_banks[0].size) {
        return NULL;
    }

    return g_banks[0].data + offset;
}

// Get bank size in bytes
size_t memory_bank_get_size() {
    return g_banks[0].size;
}


#ifdef __cplusplus
extern "C" {
#endif

/**
 * DPI Callback: Called from RTL when DMA read access
 */
int dpi_bank_read32_legacy(long long offset) {
    uint8_t data[4] = {0};
    void* src = NULL;

    if (memory_bank_is_available()) {
        src = memory_bank_get_ptr((size_t)offset);
    } else if (g_os_ram != NULL && offset >= 0 &&
               (size_t)offset + 4 <= g_os_ram_size) {
        src = g_os_ram + offset;
    }
    if (src == NULL) {
        return 0;
    }

    //printf("MEM READ RESULT: Address=%p, DPI return=%x, Readback=%x, Match=%d\n",
    //        (void*)((size_t)offset), *(unsigned int*)data, *(unsigned int*)data, 1);
    //fflush(stdout);
    
    memcpy(&data, src, 4);

    return  *((uint32_t*)data);
}

/**
 * DPI Callback: Called from RTL when DMA write access
 */
int dpi_bank_write32_legacy(long long offset, int value) {
    void* dst = NULL;

    if (memory_bank_is_available()) {
        dst = memory_bank_get_ptr((size_t)offset);
    } else if (g_os_ram != NULL && offset >= 0 &&
               (size_t)offset + 4 <= g_os_ram_size) {
        dst = g_os_ram + offset;
    }
    if (dst == NULL) {
        return -1;
    }
    //printf("MEM WRITE RESULT: Address=%p, Write data=%x, Write rc=0, Readback=%x, Match=1\n",
    //        (void*)((size_t)offset), *(unsigned int*)&value, *(unsigned int*)&value);
    memcpy(dst,  &value, 4);
    //fflush(stdout);
       
    return 0;
}


/**
 * DPI Callback: Called from RTL when DMA transfer completes
 */
void dma_done_callback(void)
{
    /* The chip's interrupt line (ch_done rising edge). Runs in the
     * co-processor thread inside the RTL eval(): it sends the interrupt to
     * the CPU that gave the command; the handler runs there. */
    struct dma_req *r = g_cur;
    if (r != NULL) {
        atomic_store(&g_cb_fired, 1);
        dma_raise_irq(r, 0);
    } else {
        printf("DMA completed\n");   /* standalone testbench, no CPU */
        fflush(stdout);
    }
}

#ifdef __cplusplus
}
#endif
