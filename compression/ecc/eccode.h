
#ifndef _ECCODE_H_
#define _ECCODE_H_

#include <stddef.h>

/* standard bit vector macros */
#define BIT_TEST(c,i)  (c[(i)/8] &   (1 << ((i) % 8)))
#define BIT_SET(c,i)   (c[(i)/8] |=  (1 << ((i) % 8)))
#define BIT_CLEAR(c,i) (c[(i)/8] &= ~(1 << ((i) % 8)))

#define MODE_ENCODE  0
#define MODE_DECODE  1
#define MODE_NOISE   2
#define MODE_ENCODEX 3
#define MODE_DECODEX 4
#define MODE_NOISEX  5

int ecc_recode(int mode, unsigned char *ib, size_t ilen, unsigned char *ob);
size_t ecc_compute_olen(int mode, size_t ilen, size_t *ibits, size_t *obits);

#endif /* _ECCODE_H_ */
