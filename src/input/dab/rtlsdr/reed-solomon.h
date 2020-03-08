/* Include file to configure the RS codec for character symbols
 *
 * Copyright 2002, Phil Karn, KA9Q
 * May be used under the terms of the GNU General Public License (GPL)
 */

#ifndef	__REED_SOLOMON
#define	__REED_SOLOMON

#include "tvheadend.h"

typedef struct galois galois_t;

struct galois {
	uint16_t mm;		/* Bits per symbol */
	uint16_t gfpoly;
	uint16_t codeLength;	/* Symbols per block (= (1<<mm)-1) */
	uint16_t d_q;
	uint16_t *alpha_to;	/* log lookup table */
	uint16_t *index_of;	/* Antilog lookup table */
};

typedef struct reedSolomon reedSolomon_t;

struct reedSolomon {
	galois_t* myGalois;
	uint16_t symsize;		/* Bits per symbol */
	uint16_t codeLength;		/* Symbols per block (= (1<<mm)-1) */
	uint8_t *generator;	/* Generator polynomial */
	uint16_t nroots;	/* Number of generator roots = number of parity symbols */
	uint8_t fcr;		/* First consecutive root, index form */
	uint8_t prim;		/* Primitive element, index form */
	uint8_t iprim;		/* prim-th root of 1, index form */
};

reedSolomon_t* init_reedSolomon(uint16_t symsize,
	                          uint16_t gfpoly,
	                          uint16_t fcr,
	                          uint16_t prim,
	                          uint16_t nroots);
int16_t	reedSolomon_dec (reedSolomon_t* res, const uint8_t *r, uint8_t *d, int16_t cutlen);

void destroy_reedSolomon(reedSolomon_t* res);

#endif
