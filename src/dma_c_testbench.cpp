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
#include "dma.h"

/*
 * dpi_dma_copy - perform DMA copy via DPI
 * src_offset: source PHYSICAL offset in RAM
 * dst_offset: destination PHYSICAL offset in RAM
 * length: number of bytes to copy (the last beat can be partial)
 * caller is unused on purpose: the process may be gone when the copy runs.
 */
int dpi_dma_copy(struct pcb_t *caller, uint64_t src_offset, uint64_t dst_offset, uint64_t length) {
    (void)caller;
    if (length == 0 || length > UINT32_MAX)
        return -1;
    void *src = memory_bus_ptr(src_offset, length);
    void *dst = memory_bus_ptr(dst_offset, length);

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
    for (uint64_t i = 0; result == 0 && i < length; ++i)
        if (((uint8_t*)dst)[i] != (uint8_t)((i % pattern_length) + 1))
            result = -1;
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
    if (memory_bank_init(1024 * 1024) == MEMORY_BANK_INVALID)
        return EXIT_FAILURE;

    /* Initialize the internal Verilator RTL simulation
     * This is necessary to ensure that the RTL simulation is ready for DPI calls.
     */
    if (rtl_dma_init_simulation() != 0) {
        printf("Cannot initialize RTL simulation\n");
        memory_bank_cleanup();
        return 1;
    }

    int result = do_dma(NULL, source, destination, length);
    rtl_dma_cleanup_simulation();
    memory_bank_cleanup();
    printf("DMA testbench: %llu bytes, %s\n", (unsigned long long)length,
           result == 0 ? "PASS" : "FAIL");
    return result == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
#endif
