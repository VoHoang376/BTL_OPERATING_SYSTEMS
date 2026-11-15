#include <stdio.h>
#include <string.h>
#include "libstd.h"
#include "syscall.h"
#include "sched.h"
#include "mm64.h"
#include "mm.h"

int copy_from_user(uint32_t pid, void *kdst, addr_t usrc, uint32_t size) {
    if (!kdst || size == 0) return -1;

    /*
     * Yêu cầu TV2: Giao tiếp qua PID, ngăn chặn người dùng truy cập trái phép Kernel Space
     */
    struct pcb_t *proc = get_pcb_by_pid(pid);
    if (!proc || !proc->mm) {
        printf("[libstd] copy_from_user Error: Invalid PID %u\n", pid);
        return -1;
    }

    /* Kiểm tra ranh giới User / Kernel: địa chỉ User phải < 0x80000000 */
    if (usrc >= 0x80000000) {
        printf("[Security Warning] PID %u attempted illegal access to Kernel Space at 0x%x!\n", pid, usrc);
        return -1;
    }

    uint8_t *dst_buf = (uint8_t *)kdst;
    for (uint32_t i = 0; i < size; i++) {
        uint64_t paddr = 0;
        if (mm64_translate_address(proc->mm, (uint64_t)(usrc + i), &paddr) != 0) {
            printf("[libstd] copy_from_user: Page fault at VA 0x%x for PID %u\n", usrc + i, pid);
            return -1;
        }
        BYTE b = 0;
        MEMPHY_read(kernel.mram, (int)paddr, &b);
        dst_buf[i] = b;
    }

    printf("[libstd] copy_from_user: Copied %u bytes from PID %u VA 0x%x -> Kernel %p\n",
           size, pid, usrc, kdst);
    return 0;
}

int copy_to_user(uint32_t pid, addr_t udst, const void *ksrc, uint32_t size) {
    if (!ksrc || size == 0) return -1;

    struct pcb_t *proc = get_pcb_by_pid(pid);
    if (!proc || !proc->mm) {
        printf("[libstd] copy_to_user Error: Invalid PID %u\n", pid);
        return -1;
    }

    /* Kiểm tra ranh giới User / Kernel */
    if (udst >= 0x80000000) {
        printf("[Security Warning] PID %u attempted illegal write to Kernel Space at 0x%x!\n", pid, udst);
        return -1;
    }

    const uint8_t *src_buf = (const uint8_t *)ksrc;
    for (uint32_t i = 0; i < size; i++) {
        uint64_t paddr = 0;
        if (mm64_translate_address(proc->mm, (uint64_t)(udst + i), &paddr) != 0) {
            printf("[libstd] copy_to_user: Page fault at VA 0x%x for PID %u\n", udst + i, pid);
            return -1;
        }
        MEMPHY_write(kernel.mram, (int)paddr, src_buf[i]);
    }

    printf("[libstd] copy_to_user: Copied %u bytes from Kernel %p -> PID %u VA 0x%x\n",
           size, ksrc, pid, udst);
    return 0;
}

int sys_call_invoke(int syscall_no, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3) {
    struct sc_regs regs = { a0, a1, a2, a3 };
    return syscall(syscall_no, NULL, &regs);
}
