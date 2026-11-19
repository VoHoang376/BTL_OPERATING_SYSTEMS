#ifndef DMA_H
#define DMA_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <pthread.h>

/* DMA Configuration according to Assignment Section 2.3 & Table 3 */
#define DMA_ADDR_WIDTH 64
#define DMA_DATA_WIDTH 32

/* DMA Operational States (Table 3 - STATE: 2 bits) */
enum dma_state_t {
    DMA_STATE_IDLE  = 0,
    DMA_STATE_BUSY  = 1,
    DMA_STATE_DONE  = 2,
    DMA_STATE_ERROR = 3
};

/* DMA Controller Pinout and State Interface (Table 3) */
struct dma_controller_t {
    /* Inputs */
    int clk;                 /* 1-bit Clock signal */
    int rst_n;               /* 1-bit Reset (Active low) */
    int ch_en;               /* 1-bit Channel enable signal */
    int ch_pause;            /* 1-bit Channel pause signal */
    int dat_valid;           /* 1-bit Indicates input lines are valid */
    uint64_t dat_0;          /* 64-bit Address line 0 (Source Address) */
    uint64_t dat_1;          /* 64-bit Address line 1 (Destination Address) */
    uint32_t dat_2;          /* 32-bit Data/Length line 2 (Transfer size) */
    uint32_t dat_3;          /* 32-bit Data line 3 (Reserved/Extra) */

    /* Outputs */
    int irq;                 /* 1-bit Interrupt request line to CPU */
    int state;               /* 2-bit Current operational state (enum dma_state_t) */
    int ch_done;             /* 1-bit Asserted when transfer finishes */
    int ch_error;            /* 1-bit Asserted if an error occurs */
    uint32_t ch_xfer_count;  /* 32-bit Total count of data transfers completed */
    int dat_ready;           /* 1-bit DMA module readiness indication */

    /* Internal Synchronization & Thread control */
    pthread_mutex_t lock;
    pthread_cond_t cond;
    pthread_t thread;
    bool is_busy;
    bool running;
};

extern struct dma_controller_t dma_engine;
extern int dma_irq_pid;

/* --- Thành viên 3: Core DMA & Bus Addressing Functions --- */
int dma_init(void);
void dma_stop(void);
int dma_trigger_async(uint64_t src, uint64_t dst, uint32_t len);
int dma_copy(int bank, uint64_t src_offset, uint64_t dst_offset, uint64_t length);
void *get_ptr(size_t bus_offset);

/* --- Thành viên 4: Interrupts & CPU Integration Functions --- */
int dma_check_irq(void);
void dma_isr(int cpu_id);

#endif /* DMA_H */