#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "loader.h"
#include "common.h"

static uint32_t next_pid = 1;

static enum ins_opcode_t parse_opcode(const char *op_str) {
    if (strcmp(op_str, "calc") == 0) return CALC;
    if (strcmp(op_str, "alloc") == 0) return ALLOC;
    if (strcmp(op_str, "kmalloc") == 0) return KMALLOC;
    if (strcmp(op_str, "free") == 0) return FREE;
    if (strcmp(op_str, "read") == 0) return READ;
    if (strcmp(op_str, "write") == 0) return WRITE;
    if (strcmp(op_str, "copy_from_user") == 0) return COPY_FROM_USER;
    if (strcmp(op_str, "copy_to_user") == 0) return COPY_TO_USER;
    if (strcmp(op_str, "dma_memcpy") == 0) return DMA_MEMCPY;
    if (strcmp(op_str, "syscall") == 0) return SYSCALL;
    return CALC;
}

struct pcb_t *load(const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "[Loader] Error: Cannot open program file: %s\n", path);
        return NULL;
    }

    struct pcb_t *proc = (struct pcb_t *)malloc(sizeof(struct pcb_t));
    if (!proc) {
        fclose(fp);
        return NULL;
    }
    memset(proc, 0, sizeof(struct pcb_t));

    proc->pid = next_pid++;
    strncpy(proc->path, path, sizeof(proc->path) - 1);
    proc->pc = 0;
    proc->bp = 0;

    int num_ins = 0;
    if (fscanf(fp, "%u %d", &proc->priority, &num_ins) != 2) {
        fprintf(stderr, "[Loader] Invalid header in %s\n", path);
        free(proc);
        fclose(fp);
        return NULL;
    }

#ifdef MLQ_SCHED
    proc->prio = proc->priority;
#endif

    proc->code = (struct code_seg_t *)malloc(sizeof(struct code_seg_t));
    proc->code->size = num_ins;
    proc->code->text = (struct instruction_t *)calloc(num_ins, sizeof(struct instruction_t));

    char op[32];
    for (int i = 0; i < num_ins; i++) {
        if (fscanf(fp, "%31s", op) != 1) break;
        proc->code->text[i].opcode = parse_opcode(op);

        switch (proc->code->text[i].opcode) {
            case CALC:
                break;
            case ALLOC:
            case KMALLOC:
                fscanf(fp, "%llu %llu", (unsigned long long *)&proc->code->text[i].arg0, (unsigned long long *)&proc->code->text[i].arg1);
                break;
            case FREE:
                fscanf(fp, "%llu", (unsigned long long *)&proc->code->text[i].arg0);
                break;
            case READ:
            case WRITE:
                fscanf(fp, "%llu %llu %llu", (unsigned long long *)&proc->code->text[i].arg0, (unsigned long long *)&proc->code->text[i].arg1, (unsigned long long *)&proc->code->text[i].arg2);
                break;
            case COPY_FROM_USER:
            case COPY_TO_USER:
            case DMA_MEMCPY:
                fscanf(fp, "%llu %llu %llu %llu", (unsigned long long *)&proc->code->text[i].arg0, (unsigned long long *)&proc->code->text[i].arg1, (unsigned long long *)&proc->code->text[i].arg2, (unsigned long long *)&proc->code->text[i].arg3);
                break;
            case SYSCALL:
                fscanf(fp, "%llu %llu", (unsigned long long *)&proc->code->text[i].arg0, (unsigned long long *)&proc->code->text[i].arg1);
                break;
        }
    }

    fclose(fp);
    return proc;
}
