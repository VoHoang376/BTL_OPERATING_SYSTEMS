#include <stdio.h>
#include "mem.h"

/* Legacy RAM / Virtual memory routines (kept for compatibility) */
int mem_init(void) {
    printf("[Legacy MEM] Obsolete mem module initialized.\n");
    return 0;
}
