/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 *
 * demacOprx release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 *
 * system bus protoype header file
 */

#ifndef SYSTEM_BUS_H
#define SYSTEM_BUS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Maximum number of memory banks supported
#define MAX_MEMORY_BANKS 8

// Memory bank handle type
typedef int memory_bank_t;

// Invalid memory bank handle
#define MEMORY_BANK_INVALID (-1)

// Initialize a memory bank with given size in bytes
memory_bank_t memory_bank_init(size_t size_bytes);

// Cleanup all memory banks
void memory_bank_cleanup(void);

// Get pointer to locate data at offset
void* memory_bank_get_ptr(uint64_t offset);

// Get bank size in bytes
size_t memory_bank_get_size(void);

// Check if memory bank is initialized
int memory_bank_is_available(void);

// DPI Callback: Called from RTL when DMA read access
int dpi_bank_read32_legacy(long long offset);

// DPI Callback: Called from RTL when DMA write access
int dpi_bank_write32_legacy(long long offset, int value);

struct dma_segment {
    uint64_t src, dst, len;  /* physical byte offsets and length */
};

void memory_bus_use_os_ram(void *storage, size_t size);
void *memory_bus_os_ram_ptr(uint64_t offset, uint64_t len);
void *memory_bus_ptr(uint64_t offset, uint64_t length);
int dma_bus_begin(uint64_t source, uint64_t destination, uint64_t length);
int dma_bus_end(void);

/* Install SIGRTMIN in main before starting threads. Only simulated CPUs
 * enable it; the worker directs each completion to the CPU that submitted it. */
int dma_irq_install(unsigned max_pid);   /* max_pid = number of processes (PIDs 1..max_pid) */
int dma_register_cpu(int cpu_id);
void dma_unregister_cpu(void);
void dma_irq_cleanup(void);
/* On success the queue owns segments until the CPU acknowledges the interrupt. */
int dma_submit(uint32_t pid, int source, int destination,
               struct dma_segment *segments, size_t count);
int dma_regions_busy(uint32_t pid, int first, int second); /* first < 0: any region */
int dma_address_busy(uint32_t pid, uint64_t address, int writing);
/* Interrupt handler -> process: number of DMA transfers of this process that
 * failed since the last call (the counter is cleared). 0 = all good. */
int dma_take_error(uint32_t pid);
int dma_worker_loop(void);
void dma_queue_shutdown(void);

// DPI Callback: Called from RTL when DMA transfer completes
void dma_done_callback(void);
#ifdef __cplusplus
}
#endif

#endif // SYSTEM_BUS_H