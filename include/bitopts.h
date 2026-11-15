#ifndef BITOPTS_H
#define BITOPTS_H

#include <stdint.h>

/* Bit manipulation macros */
#define SET_BIT(target, bit)    ((target) |= (1ULL << (bit)))
#define CLR_BIT(target, bit)    ((target) &= ~(1ULL << (bit)))
#define TST_BIT(target, bit)    (((target) & (1ULL << (bit))) != 0)
#define GET_BITS(val, shift, mask) (((val) >> (shift)) & (mask))

#endif /* BITOPTS_H */
