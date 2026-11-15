#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "dma.h"
#include "common.h"
#include "os-mm.h"

struct dma_controller_t dma_engine;

/* ========================================================================= */
/* --- THÀNH VIÊN 3: CORE DMA & BUS ADDRESSING                           --- */
/* ========================================================================= */

/**
 * get_ptr: Triển khai cơ chế Bus Addressing.
 * Cho phép bộ điều khiển DMA truy cập trực tiếp vào vùng nhớ RAM vật lý
 * dựa trên địa chỉ offset của bus mà không cần đi qua MMU hay thanh ghi CPU.
 */
void *get_ptr(size_t bus_offset) {
    if (!kernel.mram || !kernel.mram->storage) {
        return NULL;
    }
    if ((int)bus_offset >= kernel.mram->maxsz) {
        return NULL;
    }
    return (void *)(kernel.mram->storage + bus_offset);
}

/**
 * dma_copy: Hàm sao chép dữ liệu trực tiếp trong bộ nhớ qua Bus.
 * Hiện thực đúng theo Software Prototype ở Appendix (Trang 33 tài liệu môn học).
 * Dữ liệu được sao chép byte-by-byte theo khuyến nghị tại Section 2.3.2.
 */
int dma_copy(int bank, uint64_t src_offset, uint64_t dst_offset, uint64_t length) {
    printf("[DMA CALL] bank=%d, src=%llu, dst=%llu, len=%llu\n",
           bank, (unsigned long long)src_offset, 
           (unsigned long long)dst_offset, (unsigned long long)length);

    void *src = get_ptr((size_t)src_offset);
    void *dst = get_ptr((size_t)dst_offset);

    if (!src || !dst) {
        printf("[DMA Error] Invalid bus address! src=%p, dst=%p\n", src, dst);
        return -1;
    }

    uint8_t *s = (uint8_t *)src;
    uint8_t *d = (uint8_t *)dst;

    /* In dữ liệu trước khi copy (Debug Print theo prototype trang 33) */
    printf("[DMA Debug] Before copy:\n");
    printf("   src[0] = 0x%02x, src[1] = 0x%02x\n", s[0], length > 1 ? s[1] : 0);
    printf("   dst[0] = 0x%02x, dst[1] = 0x%02x\n", d[0], length > 1 ? d[1] : 0);

    printf("[DMA Core] Calling DMA copy directly via Memory Bus (byte-by-byte)...\n");
    for (size_t i = 0; i < (size_t)length; i++) {
        d[i] = s[i];
    }

    /* In dữ liệu sau khi copy */
    printf("[DMA Debug] After copy:\n");
    printf("   src[0] = 0x%02x, src[1] = 0x%02x\n", s[0], length > 1 ? s[1] : 0);
    printf("   dst[0] = 0x%02x, dst[1] = 0x%02x\n", d[0], length > 1 ? d[1] : 0);

    return 0;
}

/**
 * dma_trigger_async: Khung giao tiếp bất đồng bộ từ CPU sang DMA.
 * CPU ghi các thanh ghi cấu hình (dat_0, dat_1, dat_2), kích hoạt ch_en và dat_valid.
 * Hàm trả về ngay lập tức để CPU không bị nghẽn (non-blocking).
 */
int dma_trigger_async(uint64_t src, uint64_t dst, uint32_t len) {
    pthread_mutex_lock(&dma_engine.lock);

    /* Cài đặt các thanh ghi phần cứng (Table 3) */
    dma_engine.dat_0 = src;          /* Source Address (64-bit) */
    dma_engine.dat_1 = dst;          /* Destination Address (64-bit) */
    dma_engine.dat_2 = len;          /* Transfer Length (32-bit) */
    dma_engine.dat_valid = 1;        /* Dữ liệu đầu vào hợp lệ */
    dma_engine.ch_en = 1;            /* Kích hoạt kênh truyền DMA */
    dma_engine.ch_done = 0;          /* Reset cờ hoàn thành */
    dma_engine.ch_error = 0;         /* Reset cờ lỗi */
    dma_engine.state = DMA_STATE_BUSY;
    dma_engine.is_busy = true;

    printf("[CPU -> DMA] Offloading transfer: src=0x%llx, dst=0x%llx, len=%u (Non-blocking)\n",
           (unsigned long long)src, (unsigned long long)dst, len);

    /* Đánh thức luồng phần cứng DMA */
    pthread_cond_signal(&dma_engine.cond);

    pthread_mutex_unlock(&dma_engine.lock);
    return 0;
}

/* ========================================================================= */
/* --- THÀNH VIÊN 4: INTERRUPT HANDLING & CPU INTEGRATION                 --- */
/* ========================================================================= */

/**
 * dma_worker: Luồng mô phỏng phần cứng Co-processor DMA hoạt động độc lập.
 * Thực hiện sao chép dữ liệu chạy ngầm dưới nền và sinh tín hiệu ngắt IRQ khi hoàn thành.
 */
static void *dma_worker(void *arg) {
    (void)arg;
    while (dma_engine.running) {
        pthread_mutex_lock(&dma_engine.lock);
        while (!dma_engine.is_busy && dma_engine.running) {
            pthread_cond_wait(&dma_engine.cond, &dma_engine.lock);
        }

        if (!dma_engine.running) {
            pthread_mutex_unlock(&dma_engine.lock);
            break;
        }

        /* Mô phỏng độ trễ truyền dữ liệu trên bus phần cứng */
        usleep(30000); /* 30ms latency */

        /* Thực hiện sao chép dữ liệu */
        int res = dma_copy(0, dma_engine.dat_0, dma_engine.dat_1, dma_engine.dat_2);

        if (res == 0) {
            dma_engine.ch_done = 1;
            dma_engine.ch_error = 0;
            dma_engine.ch_xfer_count++;
            dma_engine.state = DMA_STATE_DONE;
            printf("[DMA Hardware] Transfer finished successfully. Raising IRQ signal!\n");
        } else {
            dma_engine.ch_done = 0;
            dma_engine.ch_error = 1;
            dma_engine.state = DMA_STATE_ERROR;
            printf("[DMA Hardware] Transfer failed. Raising IRQ with error status!\n");
        }

        /* Bật tín hiệu ngắt phần cứng IRQ báo cho CPU */
        dma_engine.irq = 1;

        /* Reset trạng thái kích hoạt */
        dma_engine.ch_en = 0;
        dma_engine.dat_valid = 0;
        dma_engine.dat_ready = 1;
        dma_engine.is_busy = false;

        pthread_mutex_unlock(&dma_engine.lock);
    }
    return NULL;
}

/**
 * dma_init: Khởi tạo mô-đun phần cứng DMA và khởi chạy luồng nền.
 */
int dma_init(void) {
    memset(&dma_engine, 0, sizeof(struct dma_controller_t));
    dma_engine.clk = 1;
    dma_engine.rst_n = 1;
    dma_engine.state = DMA_STATE_IDLE;
    dma_engine.dat_ready = 1;
    dma_engine.running = true;

    pthread_mutex_init(&dma_engine.lock, NULL);
    pthread_cond_init(&dma_engine.cond, NULL);
    pthread_create(&dma_engine.thread, NULL, dma_worker, NULL);
    printf("[DMA Init] DMA Co-processor engine initialized successfully.\n");
    return 0;
}

/**
 * dma_stop: Dừng luồng phần cứng DMA khi hệ thống tắt.
 */
void dma_stop(void) {
    pthread_mutex_lock(&dma_engine.lock);
    dma_engine.running = false;
    dma_engine.is_busy = false;
    pthread_cond_signal(&dma_engine.cond);
    pthread_mutex_unlock(&dma_engine.lock);

    pthread_join(dma_engine.thread, NULL);
    pthread_mutex_destroy(&dma_engine.lock);
    pthread_cond_destroy(&dma_engine.cond);
}

/**
 * dma_check_irq: CPU kiểm tra đường dây ngắt IRQ từ DMA.
 * Trả về 1 nếu tín hiệu ngắt đang được kéo lên mức tích cực (Asserted).
 */
int dma_check_irq(void) {
    int has_irq = 0;
    pthread_mutex_lock(&dma_engine.lock);
    if (dma_engine.irq == 1) {
        has_irq = 1;
    }
    pthread_mutex_unlock(&dma_engine.lock);
    return has_irq;
}

/**
 * dma_isr: Interrupt Service Routine (Trình phục vụ ngắt) được CPU gọi khi phát hiện ngắt.
 * Xác nhận ngắt (Acknowledge), in thông báo xử lý và hạ đường tín hiệu ngắt (De-assert IRQ).
 */
void dma_isr(int cpu_id) {
    pthread_mutex_lock(&dma_engine.lock);
    if (dma_engine.irq == 0) {
        /* Ngắt đã được nhân CPU khác phục vụ xong */
        pthread_mutex_unlock(&dma_engine.lock);
        return;
    }

    printf("[CPU %d] [ISR] Handled DMA Interrupt! Transfers completed so far: %u\n",
           cpu_id, dma_engine.ch_xfer_count);

    /* Acknowledge và hạ tín hiệu ngắt */
    dma_engine.irq = 0;
    dma_engine.state = DMA_STATE_IDLE;
    dma_engine.ch_done = 0;
    pthread_mutex_unlock(&dma_engine.lock);
}
