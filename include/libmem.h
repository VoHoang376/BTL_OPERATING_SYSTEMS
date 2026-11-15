#ifndef LIBMEM_H
#define LIBMEM_H

#include <stdint.h>
#include "common.h"

/* Userspace memory API */
int libmem_alloc(struct pcb_t *proc, uint32_t size, uint32_t reg_idx);
int libmem_free(struct pcb_t *proc, uint32_t reg_idx);
int libmem_read(struct pcb_t *proc, uint32_t src_reg, uint32_t offset, uint32_t dst_reg);
int libmem_write(struct pcb_t *proc, uint32_t data_reg, uint32_t dst_reg, uint32_t offset);

#endif /* LIBMEM_H */
