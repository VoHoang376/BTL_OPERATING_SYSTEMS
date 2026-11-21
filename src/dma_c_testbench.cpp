/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 *
 * demacOprx release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 *
 * DMA C Testbench 
 */

/**
 * Build modes:
 *   DMA_MAIN=1 - Build as standalone executable with main()
 *   DMA_MAIN=0 - Build as library module with do_dma()
 */
   
#ifndef DMA_MAIN
#define DMA_MAIN 0
#endif


#include "sysbus.h"
#include "dpi_rtl.h"
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cstdbool>
#include <thread>
#include <atomic>
#include <mutex>

// Generated DPI functions from RTL
#include <verilated.h>
#include <verilated_vcd_c.h>
#include "Vdma_top.h"
#include "Vdma_top__Dpi.h"  

#if DMA_MAIN == 1
extern "C" {
#include "common.h"
#include "os-mm.h"
}
#endif

// RTL Direct Test - Drive Verilator RTL directly
extern "C" int dpi_rtl_dma_copy(uint64_t src_offset, uint64_t dst_offset, uint64_t length);
extern "C" int dpi_rtl_dma_is_available(void);

/*
 * There is ONE DMA chip (one global Verilator model, reset on every transfer).
 * Whatever thread drives it must hold this lock for the whole transfer.
 */
static std::mutex g_chip_lock;

/*
 * dpi_dma_copy - perform DMA copy via DPI
 * src_offset: source PHYSICAL offset in RAM
 * dst_offset: destination PHYSICAL offset in RAM
 * length: number of bytes to copy (multiple of 4: the chip moves 32-bit words)
 * caller is unused on purpose: the process may be gone when the copy runs.
 * Caller must hold g_chip_lock.
 */
int dpi_dma_copy(struct pcb_t *caller, uint64_t src_offset, uint64_t dst_offset, uint64_t length) {
    (void)caller;
    void *src = NULL;
    void *dst = NULL;

    if (length == 0 || (length % 4) != 0) {
        printf("dpi_dma_copy: length %llu is not a positive multiple of 4\n",
               (unsigned long long) length);
        return -1;
    }

    if (memory_bank_is_available()) {
        /* standalone testbench: private memory bank */
        src = memory_bank_get_ptr(src_offset);
        dst = memory_bank_get_ptr(dst_offset);
    } else {
        /* ./os: the chip reads/writes the real OS RAM (registered by main()) */
        src = memory_bus_os_ram_ptr(src_offset, length);
        dst = memory_bus_os_ram_ptr(dst_offset, length);
    }

    if (src == NULL || dst == NULL) {
        printf("dpi_dma_copy: cannot map source or destination "
               "(src=%llu dst=%llu len=%llu)\n",
               (unsigned long long) src_offset,
               (unsigned long long) dst_offset,
               (unsigned long long) length);
        return -1;
    }

#if DMA_MAIN == 1
    printf("dpi_dma_copy: src=%llu, dst=%llu, len=%llu\n", 
           (unsigned long long) src_offset, 
           (unsigned long long) dst_offset,
           (unsigned long long) length);

    /* Testbench only: fill the source with a known pattern 1..255 */
    uint8_t* psrc = (uint8_t*)src;
    uint64_t pattern_length = (length <= 255) ? length : 255;
    for (uint64_t i = 0; i < length; i++) {
        psrc[i] = (uint8_t)((i % pattern_length) + 1);
    }

    uint64_t show = (length < 8) ? length : 8;
    printf("Before copy:\n");
    for (uint64_t i = 0; i < show; i++)
        printf("  src[%llu] = 0x%02x   dst[%llu] = 0x%02x\n",
               (unsigned long long) i, ((uint8_t*)src)[i],
               (unsigned long long) i, ((uint8_t*)dst)[i]);
    printf("\nCalling RTL DMA...\n");
#else
    printf("[DMA] %llu bytes: phys 0x%llx -> 0x%llx\n",
           (unsigned long long) length,
           (unsigned long long) src_offset,
           (unsigned long long) dst_offset);
#endif

    // Call the RTL DMA function directly with provided parameters
    int result = dpi_rtl_dma_copy(src_offset, dst_offset, length);

#if DMA_MAIN == 1
    printf("\nAfter copy:\n");
    for (uint64_t i = 0; i < show; i++)
        printf("  src[%llu] = 0x%02x   dst[%llu] = 0x%02x\n",
               (unsigned long long) i, ((uint8_t*)src)[i],
               (unsigned long long) i, ((uint8_t*)dst)[i]);
#endif

    return result;
}

/*
 * do_dma - run ONE DMA transfer on the RTL chip and return when it is done.
 *
 * It is synchronous by itself. In ./os it is only called by the co-processor
 * thread (dma_worker_loop), so CPU threads never wait for it: they use
 * dma_submit() and continue. The standalone testbench calls it directly.
 * source / destination are physical RAM offsets.
 */
extern "C" int do_dma(struct pcb_t *caller,uint64_t source, uint64_t destination, uint64_t length) {
    std::lock_guard<std::mutex> guard(g_chip_lock);

    /* init is idempotent; done under the lock so it can never race */
    if (rtl_dma_init_simulation() != 0) {
        return -1;
    }
    return dpi_dma_copy(caller, source, destination, length);
}

#if DMA_MAIN == 1
/* main function for independent standalone dma_testbench build
 * [Usage]: ./dma_testbench [source_offset] [destination_offset] [length]
 * source: source memory offset
 * destination: destination memory offset
 * length: number of bytes to copy
 */
int main(int argc, char** argv) {
    // Default test parameters
    uint64_t source = 0x1000;
    uint64_t destination = 0x2000;
    uint64_t length = 512;

    /*
     *  Only accept command line arguments if provided
     *  [Usage]: ./dma_testbench [source_offset] [destination_offset] [length]
     */
    if (argc >= 4) {
        source = strtoull(argv[1], NULL, 0);
        destination = strtoull(argv[2], NULL, 0);
        length = strtoull(argv[3], NULL, 0);
    }
    
    printf("Running dma_testbench source=0x%llx, destination=0x%llx, length=%llu\n",
           (unsigned long long)source, (unsigned long long)destination, (unsigned long long)length);

    /*
     * Initialize the system memory bank
     * This is necessary to independent standalone dma_testbench build .
     */
    memory_bank_init(1024 * 1024);

    /* Initialize the internal Verilator RTL simulation
     * This is necessary to ensure that the RTL simulation is ready for DPI calls.
     */
    if (rtl_dma_init_simulation() != 0) {
        printf("Cannot initialize RTL simulation\n");
        return 1;
    }

    /*
    * Create process control block for the caller
    * Properly initialize all required fields
    */
    struct pcb_t *caller = (struct pcb_t *)calloc(1, sizeof(struct pcb_t));
    caller->krnl = (struct krnl_t *)calloc(1, sizeof(struct krnl_t));
    caller->krnl->mm = (struct mm_struct *)calloc(1, sizeof(struct mm_struct));
    caller->krnl->mram = (struct memphy_struct *)calloc(1, sizeof(struct memphy_struct));
    caller->krnl->active_mswp = (struct memphy_struct *)calloc(1, sizeof(struct memphy_struct));
    caller->mm = caller->krnl->mm;
    caller->pid = 0;  // Set a default PID

    int result = do_dma(caller, source, destination, length);

    // Cleanup
    free(caller->krnl->active_mswp);
    free(caller->krnl->mram);
    free(caller->krnl->mm);
    free(caller->krnl);
    free(caller);

    return result;
}
#endif
