#include <stdio.h>
#include "libmem.h"
#include "libstd.h"
#include "mm.h"
#include "mm64.h"

int libmem_alloc(struct pcb_t *proc, uint32_t size, uint32_t reg_idx) {
    if (!proc || reg_idx >= 10) return -1;

    /* Nâng sbrk qua VMA limit */
    inc_vma_limit(proc, 0, size);

    /* Địa chỉ ảo cấp phát cho tiến trình */
    addr_t vaddr = proc->bp;
    proc->bp += size;

    /* Cấp phát các frame vật lý tương ứng */
    int num_pages = (size + PAGING_PAGESZ - 1) / PAGING_PAGESZ;
    struct framephy_struct *frames = NULL;
    alloc_pages_range(proc, num_pages, &frames);

    /* Ánh xạ vào bảng phân trang 5 cấp 64-bit */
    struct vm_rg_struct rg;
    vmap_page_range(proc, vaddr, num_pages, frames, &rg);

    /* Lưu địa chỉ ảo vào thanh ghi */
    proc->regs[reg_idx] = vaddr;
    printf("[libmem] ALLOC: Allocated %u bytes at VA 0x%x for PID %u -> reg[%u]\n",
           size, vaddr, proc->pid, reg_idx);

    return 0;
}

int libmem_free(struct pcb_t *proc, uint32_t reg_idx) {
    if (!proc || reg_idx >= 10) return -1;

    addr_t vaddr = proc->regs[reg_idx];
    printf("[libmem] FREE: Released region at VA 0x%x (reg[%u]) for PID %u\n",
           vaddr, reg_idx, proc->pid);
    proc->regs[reg_idx] = 0;
    return 0;
}

int libmem_read(struct pcb_t *proc, uint32_t src_reg, uint32_t offset, uint32_t dst_reg) {
    if (!proc || src_reg >= 10 || dst_reg >= 10) return -1;

    addr_t vaddr = proc->regs[src_reg] + offset;
    uint64_t paddr = 0;

    /* Dịch địa chỉ ảo sang địa chỉ vật lý */
    if (mm64_translate_address(proc->mm, (uint64_t)vaddr, &paddr) != 0) {
        printf("[libmem] READ Page Fault! Invalid virtual address 0x%x for PID %u\n", vaddr, proc->pid);
        return -1;
    }

    BYTE value = 0;
    MEMPHY_read(kernel.mram, (int)paddr, &value);
    proc->regs[dst_reg] = value;

    printf("[libmem] READ: PID %u read byte 0x%02x from VA 0x%x (PA 0x%llx) -> reg[%u]\n",
           proc->pid, value, vaddr, (unsigned long long)paddr, dst_reg);
    return 0;
}

int libmem_write(struct pcb_t *proc, uint32_t data_reg, uint32_t dst_reg, uint32_t offset) {
    if (!proc || dst_reg >= 10) return -1;

    addr_t vaddr = proc->regs[dst_reg] + offset;
    uint64_t paddr = 0;

    /* Dịch địa chỉ ảo sang địa chỉ vật lý */
    if (mm64_translate_address(proc->mm, (uint64_t)vaddr, &paddr) != 0) {
        printf("[libmem] WRITE Page Fault! Invalid virtual address 0x%x for PID %u\n", vaddr, proc->pid);
        return -1;
    }

    /* Giá trị ghi: lấy từ thanh ghi nếu hợp lệ hoặc lấy giá trị trực tiếp */
    BYTE value = (data_reg < 10 && proc->regs[data_reg] != 0) ? (BYTE)proc->regs[data_reg] : (BYTE)data_reg;
    MEMPHY_write(kernel.mram, (int)paddr, value);

    printf("[libmem] WRITE: PID %u wrote byte 0x%02x to VA 0x%x (PA 0x%llx)\n",
           proc->pid, value, vaddr, (unsigned long long)paddr);
    return 0;
}
