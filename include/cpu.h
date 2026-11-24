#ifndef CPU_H
#define CPU_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Execute an instruction of a process. Return 0
 * if the instruction is executed successfully.
 * A DMA dependency returns -EAGAIN and leaves the PC on this instruction.
 * Other failures return a nonzero status. */
int run(struct pcb_t * proc);

#ifdef __cplusplus
}
#endif

#endif
