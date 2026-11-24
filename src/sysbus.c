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
#include <semaphore.h>
#include <errno.h>
#include "dma.h"
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

struct dma_req {
    uint32_t pid;
    int source, destination, cpu;
    pthread_t issuer;
    struct dma_segment *segments;
    size_t count;
    struct dma_req *next;
};

static pthread_mutex_t g_q_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_q_cond = PTHREAD_COND_INITIALIZER;
static pthread_cond_t retired = PTHREAD_COND_INITIALIZER;
static struct dma_req *g_q_head, *g_q_tail;
static int g_shutdown;

/* A single chip raises a queued real-time signal to its issuing CPU.
 * Only the worker waits for the ISR to acknowledge that completion. */
#define DMA_IRQ_SIGNAL SIGRTMIN
_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "DMA IRQ needs lock-free integer atomics");
static _Thread_local volatile sig_atomic_t tls_cpu = -1;
static atomic_int irq_cpu = -1;
static atomic_uint irq_pid;
static sem_t irq_ack;

static void put_num(char *b, int *n, unsigned long v) {
    char t[24]; int k = 0;
    if (v == 0) t[k++] = '0';
    while (v) { t[k++] = (char)('0' + v % 10); v /= 10; }
    while (k) b[(*n)++] = t[--k];
}

static void dma_isr_body(int cpu, uint32_t pid, int err) {
    char b[96]; int n = 0;
    const char *s = "[IRQ] DMA done -> handler on CPU ";
    while (*s) b[n++] = *s++;
    put_num(b, &n, (unsigned long)cpu);
    s = " for pid ";
    while (*s) b[n++] = *s++;
    put_num(b, &n, pid);
    if (err) { s = " (ERROR)"; while (*s) b[n++] = *s++; }
    b[n++] = '\n';
    if (write(1, b, (size_t)n) < 0) { /* nothing safe to do */ }
}

static void dma_irq_handler(int sig, siginfo_t *si, void *uc) {
    (void)sig; (void)uc;
    int saved_errno = errno;
    int cpu = tls_cpu;
    if (si->si_code == SI_QUEUE && cpu >= 0 &&
        atomic_compare_exchange_strong(&irq_cpu, &cpu, -1)) {
        dma_isr_body(cpu, atomic_load(&irq_pid), si->si_value.sival_int);
        sem_post(&irq_ack);
    }
    errno = saved_errno;
}

static int set_irq_mask(int how) {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, DMA_IRQ_SIGNAL);
    return pthread_sigmask(how, &set, NULL);
}

int dma_irq_install(void) {
    struct sigaction sa = {0};
    sa.sa_sigaction = dma_irq_handler;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&sa.sa_mask);
    if (sem_init(&irq_ack, 0, 0) != 0)
        return -1;
    if (set_irq_mask(SIG_BLOCK) != 0 || sigaction(DMA_IRQ_SIGNAL, &sa, NULL) != 0) {
        dma_irq_cleanup();
        return -1;
    }
    return 0;
}

int dma_register_cpu(int cpu_id) {
    tls_cpu = cpu_id;
    return set_irq_mask(SIG_UNBLOCK);
}

void dma_unregister_cpu(void) {
    /* Already detached from the simulation timer. Keep the pthread alive for
     * interrupts it requested before its last process migrated or exited. */
    pthread_mutex_lock(&g_q_lock);
    for (;;) {
        struct dma_req *r = g_q_head;
        while (r && r->cpu != tls_cpu)
            r = r->next;
        if (!r)
            break;
        pthread_cond_wait(&retired, &g_q_lock);
    }
    pthread_mutex_unlock(&g_q_lock);
    set_irq_mask(SIG_BLOCK);
    tls_cpu = -1;
}

void dma_irq_cleanup(void) {
    sem_destroy(&irq_ack);
}

static int dma_raise_irq(const struct dma_req *r, int error) {
    union sigval sv = { .sival_int = error };
    atomic_store(&irq_pid, r->pid);
    atomic_store(&irq_cpu, r->cpu);
    if (pthread_sigqueue(r->issuer, DMA_IRQ_SIGNAL, sv) != 0)
        return -1;
    int rc;
    do {
        rc = sem_wait(&irq_ack);
    } while (rc != 0 && errno == EINTR);
    return rc == 0 ? error : -1;
}

int dma_submit(uint32_t pid, int source, int destination,
               struct dma_segment *segments, size_t count) {
    if (!count || !segments || tls_cpu < 0)
        return -1;
    struct dma_req *r = malloc(sizeof(*r));
    if (!r)
        return -1;
    *r = (struct dma_req){ .pid = pid, .source = source, .destination = destination,
                            .cpu = tls_cpu, .issuer = pthread_self(),
                            .segments = segments, .count = count };

    pthread_mutex_lock(&g_q_lock);
    if (g_shutdown) {
        pthread_mutex_unlock(&g_q_lock);
        free(r);
        return -1;
    }
    if (g_q_tail)
        g_q_tail->next = r;
    else
        g_q_head = r;
    g_q_tail = r;
    printf("[DMA] CPU %d submitted %zu segment(s) for PID %u\n", r->cpu, count, pid);
    pthread_cond_signal(&g_q_cond);
    pthread_mutex_unlock(&g_q_lock);
    return 0;
}

/* The active request stays at the head until the CPU has acknowledged it. */
int dma_regions_busy(uint32_t pid, int first, int second) {
    int busy = 0;
    pthread_mutex_lock(&g_q_lock);
    for (struct dma_req *r = g_q_head; r; r = r->next) {
        if (r->pid == pid && (first < 0 ||
            r->source == first || r->destination == first ||
            r->source == second || r->destination == second)) {
            busy = 1;
            break;
        }
    }
    pthread_mutex_unlock(&g_q_lock);
    return busy;
}

int dma_address_busy(uint32_t pid, uint64_t address, int writing) {
    int busy = 0;
    pthread_mutex_lock(&g_q_lock);
    for (struct dma_req *r = g_q_head; r && !busy; r = r->next) {
        if (r->pid != pid)
            continue;
        for (size_t i = 0; i < r->count; ++i) {
            const struct dma_segment *s = &r->segments[i];
            if ((address >= s->dst && address - s->dst < s->len) ||
                (writing && address >= s->src && address - s->src < s->len)) {
                busy = 1;
                break;
            }
        }
    }
    pthread_mutex_unlock(&g_q_lock);
    return busy;
}

int dma_worker_loop(void) {
    unsigned long ok = 0, failed = 0;
    for (;;) {
        pthread_mutex_lock(&g_q_lock);
        while (!g_q_head && !g_shutdown)
            pthread_cond_wait(&g_q_cond, &g_q_lock);
        struct dma_req *r = g_q_head;
        pthread_mutex_unlock(&g_q_lock);
        if (!r)
            break;

        int error = 0;
        for (size_t i = 0; i < r->count; ++i) {
            const struct dma_segment *s = &r->segments[i];
            if (do_dma(NULL, s->src, s->dst, s->len) != 0) {
                error = 1;
                break;
            }
        }
        int status = dma_raise_irq(r, error);
        if (status < 0) {
            fputs("Cannot deliver DMA interrupt to its CPU\n", stderr);
            exit(EXIT_FAILURE);
        }
        printf("[DMA] CPU %d acknowledged PID %u (%zu segments, %s)\n",
               r->cpu, r->pid, r->count, status ? "error" : "ok");
        if (status) ++failed;
        else ++ok;

        pthread_mutex_lock(&g_q_lock);
        g_q_head = r->next;
        if (!g_q_head)
            g_q_tail = NULL;
        pthread_cond_broadcast(&retired);
        pthread_mutex_unlock(&g_q_lock);
        free(r->segments);
        free(r);
    }
    printf("[DMA] interrupts handled: %lu (ok=%lu, failed=%lu)\n", ok + failed, ok, failed);
    return failed ? -1 : 0;
}

void dma_queue_shutdown(void) {
    pthread_mutex_lock(&g_q_lock);
    g_shutdown = 1;
    pthread_cond_signal(&g_q_cond);
    pthread_mutex_unlock(&g_q_lock);
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


/* Validate the entire bus access before forming a pointer. */
void *memory_bus_ptr(uint64_t offset, uint64_t length) {
    if (g_os_ram)
        return memory_bus_os_ram_ptr(offset, length);
    uint8_t *base = g_banks[0].data;
    size_t size = g_banks[0].size;
    if (!base || offset > size || length > size - offset)
        return NULL;
    return base + offset;
}

/* The single chip owns this descriptor from reset through its ch_done edge. */
static struct dma_segment transfer;
static int chip_done, bus_error;

int dma_bus_begin(uint64_t source, uint64_t destination, uint64_t length) {
    if (!length || length > UINT32_MAX ||
        !memory_bus_ptr(source, length) || !memory_bus_ptr(destination, length))
        return -1;
    transfer = (struct dma_segment){ source, destination, length };
    chip_done = bus_error = 0;
    return 0;
}

static size_t beat_size(uint64_t address, uint64_t base) {
    if (address < base || address - base >= transfer.len)
        return 0;
    uint64_t remaining = transfer.len - (address - base);
    return remaining < 4 ? (size_t)remaining : 4;
}

int dpi_bank_read32_legacy(long long offset) {
    uint32_t word = 0;
    size_t bytes = transfer.len ? beat_size(offset, transfer.src) : 4;
    if (!bytes)
        bytes = beat_size(offset, transfer.dst);  /* RTL reads back its write */
    void *src = memory_bus_ptr(offset, bytes);
    if (src && bytes)
        memcpy(&word, src, bytes);
    else
        bus_error = 1;
    return (int)word;
}

int dpi_bank_write32_legacy(long long offset, int value) {
    size_t bytes = transfer.len ? beat_size(offset, transfer.dst) : 4;
    void *dst = memory_bus_ptr(offset, bytes);
    if (!dst || !bytes) {
        bus_error = 1;
        return -1;
    }
    /* The last bus beat may have only 1..3 valid bytes. */
    memcpy(dst, &value, bytes);
    return 0;
}

void dma_done_callback(void) {
    chip_done = 1;
}

int dma_bus_end(void) {
    int status = chip_done && !bus_error ? 0 : -1;
    transfer.len = 0;
    return status;
}
