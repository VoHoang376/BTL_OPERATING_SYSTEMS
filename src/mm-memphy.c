#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mm.h"
#include "os-mm.h"

int init_memphy(struct memphy_struct *mp, int max_size, int random_flg) {
    if (!mp) return -1;
    mp->storage = (BYTE *)malloc(max_size);
    if (!mp->storage) return -1;
    memset(mp->storage, 0, max_size);

    mp->maxsz = max_size;
    mp->rdmflg = random_flg;
    mp->cursor = 0;
    mp->free_fp_list = NULL;
    mp->used_fp_list = NULL;
    pthread_mutex_init(&mp->lock, NULL);

    /* Initialize free frame physical list */
    int num_frames = max_size / PAGING_PAGESZ;
    for (int i = 0; i < num_frames; i++) {
        struct framephy_struct *fp = (struct framephy_struct *)malloc(sizeof(struct framephy_struct));
        fp->fpn = i;
        fp->fp_next = mp->free_fp_list;
        mp->free_fp_list = fp;
    }
    return 0;
}

int MEMPHY_read(struct memphy_struct *mp, int addr, BYTE *value) {
    if (!mp || !mp->storage || !value) return -1;
    if (addr < 0 || addr >= mp->maxsz) return -1;

    pthread_mutex_lock(&mp->lock);
    *value = mp->storage[addr];
    pthread_mutex_unlock(&mp->lock);
    return 0;
}

int MEMPHY_write(struct memphy_struct *mp, int addr, BYTE data) {
    if (!mp || !mp->storage) return -1;
    if (addr < 0 || addr >= mp->maxsz) return -1;

    pthread_mutex_lock(&mp->lock);
    mp->storage[addr] = data;
    pthread_mutex_unlock(&mp->lock);
    return 0;
}

int MEMPHY_format(struct memphy_struct *mp, int pagesz) {
    (void)pagesz;
    if (!mp || !mp->storage) return -1;
    pthread_mutex_lock(&mp->lock);
    memset(mp->storage, 0, mp->maxsz);
    pthread_mutex_unlock(&mp->lock);
    return 0;
}

int MEMPHY_get_freefp(struct memphy_struct *mp, int *retfpn) {
    if (!mp || !retfpn) return -1;
    pthread_mutex_lock(&mp->lock);
    if (!mp->free_fp_list) {
        pthread_mutex_unlock(&mp->lock);
        return -1; /* Out of memory frames */
    }
    struct framephy_struct *fp = mp->free_fp_list;
    mp->free_fp_list = fp->fp_next;
    *retfpn = fp->fpn;

    /* Move to used list */
    fp->fp_next = mp->used_fp_list;
    mp->used_fp_list = fp;
    pthread_mutex_unlock(&mp->lock);
    return 0;
}

int MEMPHY_put_freefp(struct memphy_struct *mp, int fpn) {
    if (!mp) return -1;
    pthread_mutex_lock(&mp->lock);
    /* Find in used_fp_list and move to free_fp_list */
    struct framephy_struct *prev = NULL;
    struct framephy_struct *curr = mp->used_fp_list;
    while (curr && curr->fpn != fpn) {
        prev = curr;
        curr = curr->fp_next;
    }
    if (curr) {
        if (prev) prev->fp_next = curr->fp_next;
        else mp->used_fp_list = curr->fp_next;

        curr->fp_next = mp->free_fp_list;
        mp->free_fp_list = curr;
    }
    pthread_mutex_unlock(&mp->lock);
    return 0;
}
