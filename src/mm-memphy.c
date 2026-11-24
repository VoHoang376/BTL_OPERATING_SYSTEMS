/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Caitoa release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

// #ifdef MM_PAGING
/*
 * PAGING based Memory Management
 * Memory physical module mm/mm-memphy.c
 */

#include "mm64.h"
#include <limits.h>
#include <stdlib.h>

static int memphy_address_valid(const struct memphy_struct *mp, addr_t addr)
{
   return mp != NULL && mp->storage != NULL && mp->maxsz > 0 &&
          addr < (addr_t)mp->maxsz;
}

/*
 *  MEMPHY_mv_csr - move MEMPHY cursor
 *  @mp: memphy struct
 *  @offset: offset
 */
int MEMPHY_mv_csr(struct memphy_struct *mp, addr_t offset)
{
   if (!memphy_address_valid(mp, offset))
      return -1;

   if ((int)offset < mp->cursor)
      mp->cursor = 0;
   while (mp->cursor < (int)offset)
      ++mp->cursor;

   return 0;
}

/*
 *  MEMPHY_seq_read - read MEMPHY device
 *  @mp: memphy struct
 *  @addr: address
 *  @value: obtained value
 */
int MEMPHY_seq_read(struct memphy_struct *mp, addr_t addr, BYTE *value)
{
   if (mp == NULL || mp->rdmflg || value == NULL ||
       MEMPHY_mv_csr(mp, addr) != 0)
      return -1;

   *value = mp->storage[mp->cursor];
   __atomic_fetch_add(&mp->data_reads, 1, __ATOMIC_RELAXED);
   return 0;
}

/*
 *  MEMPHY_read read MEMPHY device
 *  @mp: memphy struct
 *  @addr: address
 *  @value: obtained value
 */
int MEMPHY_read(struct memphy_struct *mp, addr_t addr, BYTE *value)
{
   if (mp != NULL && !mp->rdmflg)
      return MEMPHY_seq_read(mp, addr, value);

   if (!memphy_address_valid(mp, addr) || value == NULL)
      return -1;

   *value = mp->storage[addr];
   __atomic_fetch_add(&mp->data_reads, 1, __ATOMIC_RELAXED);
   return 0;
}

/*
 *  MEMPHY_seq_write - write MEMPHY device
 *  @mp: memphy struct
 *  @addr: address
 *  @data: written data
 */
int MEMPHY_seq_write(struct memphy_struct *mp, addr_t addr, BYTE value)
{
   if (mp == NULL || mp->rdmflg || MEMPHY_mv_csr(mp, addr) != 0)
      return -1;

   mp->storage[mp->cursor] = value;
   __atomic_fetch_add(&mp->data_writes, 1, __ATOMIC_RELAXED);
   return 0;
}

/*
 *  MEMPHY_write-write MEMPHY device
 *  @mp: memphy struct
 *  @addr: address
 *  @data: written data
 */
int MEMPHY_write(struct memphy_struct *mp, addr_t addr, BYTE data)
{
   if (mp != NULL && !mp->rdmflg)
      return MEMPHY_seq_write(mp, addr, data);

   if (!memphy_address_valid(mp, addr))
      return -1;

   mp->storage[addr] = data;
   __atomic_fetch_add(&mp->data_writes, 1, __ATOMIC_RELAXED);
   return 0;
}

static int push_free_frame(struct framephy_struct **head, addr_t fpn)
{
   struct framephy_struct *frame = malloc(sizeof(*frame));
   if (frame == NULL)
      return -1;

   *frame = (struct framephy_struct){ .fpn = fpn, .fp_next = *head };
   *head = frame;
   return 0;
}

/* Build the free list in frame-number order. Publish it only on success. */
int MEMPHY_format(struct memphy_struct *mp, int pagesz)
{
   int numfp = mp->maxsz / pagesz;
   struct framephy_struct *head = NULL;
   for (int i = numfp; i > 0; --i)
   {
      if (push_free_frame(&head, i - 1) != 0)
      {
         while (head != NULL)
         {
            struct framephy_struct *frame = head;
            head = frame->fp_next;
            free(frame);
         }
         return -1;
      }
   }
   mp->free_fp_list = head;
   return 0;
}

int MEMPHY_get_freefp(struct memphy_struct *mp, addr_t *retfpn)
{
   struct framephy_struct *fp = mp->free_fp_list;

   if (fp == NULL)
      return -1;

   *retfpn = fp->fpn;
   mp->free_fp_list = fp->fp_next;
   free(fp);
   return 0;
}

int MEMPHY_dump(struct memphy_struct *mp)
{
  /*
   * TODO dump memphy contnt mp->storage
   *     for tracing the memory content
   */

   return 0;
}

int MEMPHY_put_freefp(struct memphy_struct *mp, addr_t fpn)
{
   return push_free_frame(&mp->free_fp_list, fpn);
}

/*
 *  Init MEMPHY struct
 */
int init_memphy(struct memphy_struct *mp, addr_t max_size, int randomflg)
{
   if (mp == NULL)
      return -1;

   *mp = (struct memphy_struct){.rdmflg = randomflg != 0};

   if (max_size > INT_MAX)
      return -1;
   if (max_size == 0)
      return 0;

   mp->storage = calloc(max_size, sizeof(*mp->storage));
   if (mp->storage == NULL)
      return -1;

   mp->maxsz = max_size;

#ifdef MM64
   int pagesz = PAGING64_PAGESZ;
#else
   int pagesz = PAGING_PAGESZ;
#endif
   if (MEMPHY_format(mp, pagesz) != 0)
   {
      free(mp->storage);
      mp->storage = NULL;
      mp->maxsz = 0;
      return -1;
   }
   return 0;
}

// #endif
