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

// Point the bus at the OS RAM when the standalone memory bank is not used.
// Call ONCE from main() right after the OS RAM (mram) is created.
void memory_bus_use_os_ram(void *storage, size_t size);

// Pointer to OS RAM at physical offset, or NULL if [offset, offset+len) is
// outside the RAM (or no OS RAM was registered).
void *memory_bus_os_ram_ptr(uint64_t offset, uint64_t len);

/*
 * Interrupt-driven DMA (slide 22).
 *  - dma_irq_install():   main(), BEFORE creating threads: installs the
 *                         interrupt handler (sigaction) and blocks the
 *                         interrupt in every thread by default.
 *  - dma_register_cpu():  called at the start of each CPU thread: this CPU
 *                         can now receive DMA interrupts.
 *  - dma_unregister_cpu(): called when the CPU thread ends.
 *  - dma_submit():        step 1. CPU writes {src,dst,len} (PHYSICAL offsets,
 *                         len multiple of 4) to the DMA command queue and
 *                         returns AT ONCE. It never waits for completion.
 *  - dma_worker_loop():   co-processor thread: runs the chip. When the chip
 *                         finishes, an interrupt (SIGRTMIN) is sent to the
 *                         requesting CPU, whose handler runs (step 4).
 *  - dma_wait_idle():     main() only, at shutdown: wait for all interrupts.
 */
void dma_irq_install(void);
void dma_register_cpu(int cpu_id);
void dma_unregister_cpu(void);
int  dma_submit(uint32_t pid, uint64_t src_pa, uint64_t dst_pa, uint64_t len);
void dma_worker_loop(void);
void dma_wait_idle(void);
void dma_queue_shutdown(void);
unsigned long dma_completed_count(void);   /* finished OK                 */
unsigned long dma_failed_count(void);      /* finished with an error      */
unsigned long dma_irq_count(void);         /* interrupt handlers executed */
unsigned long dma_pid_irq_count(uint32_t pid);

// DPI Callback: Called from RTL when DMA transfer completes
void dma_done_callback(void);
#ifdef __cplusplus
}
#endif

#endif // SYSTEM_BUS_H