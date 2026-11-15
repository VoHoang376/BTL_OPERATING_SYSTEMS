#ifndef LOADER_H
#define LOADER_H

#include "common.h"

/* Loader interface: reads program description file and constructs a PCB */
struct pcb_t *load(const char *path);
void *loader_routine(void *arg);

#endif /* LOADER_H */
