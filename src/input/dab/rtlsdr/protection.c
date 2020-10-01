/*
*    Copyright (C) 2013 .. 2017
*    Jan van Katwijk (J.vanKatwijk@gmail.com)
*    Lazy Chair Computing
*
*    This file is part of the DAB-library
*    DAB-library is free software; you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation; either version 2 of the License, or
*    (at your option) any later version.
*
*    DAB-library is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*
*       Adapted to tvheadend by Matthias Walliczek (matthias@walliczek.de)
*/

#include "tvheadend.h"
#include "protTables.h"
#include "protection.h"

protection_t* protection_init(int16_t bitRate) {
    uint8_t     shiftRegister [9];
    int i;
    int j;
    
    protection_t* res = calloc(1, sizeof(protection_t));
    
    res->outSize = 24 * bitRate;
    
    res->indexTableSize = res->outSize * 4 + 24;
    
    initViterbi768(&res->vp, res->outSize);
    
    res->indexTable = calloc(res->indexTableSize, sizeof(uint8_t));
    memset(res->indexTable, 0, res->indexTableSize * sizeof(uint8_t));

    res->viterbiBlock = calloc(res->indexTableSize, sizeof(int16_t));
    res->disperseVector = calloc(res->outSize, sizeof (uint8_t));

    memset (shiftRegister, 1, 9);
    for (i = 0; i < res->outSize; i ++) {
        uint8_t b = shiftRegister [8] ^ shiftRegister [4];
        for (j = 8; j > 0; j--)
            shiftRegister [j] = shiftRegister [j - 1];
        shiftRegister [0] = b;
        res->disperseVector [i] = b;
    }

    return res;
}

void protection_createIndexTable(protection_t* protection, int16_t L1, const int8_t *PI1, int16_t L2, const int8_t *PI2,
    int16_t L3, const int8_t *PI3, int16_t L4, const int8_t *PI4) {
    int16_t i, j;
    int16_t viterbiCounter = 0;
    const int8_t  *PI_X;

    //
    //	according to the standard we process the logical frame
    //	with a pair of tuples
    //	(L1, PI1), (L2, PI2)
    //
    for (i = 0; i < L1; i++) {
        for (j = 0; j < 128; j++) {
            if (PI1[j % 32] != 0)
                protection->indexTable[viterbiCounter] = 1;
            viterbiCounter++;
        }
    }

    for (i = 0; i < L2; i++) {
        for (j = 0; j < 128; j++) {
            if (PI2[j % 32] != 0)
                protection->indexTable[viterbiCounter] = 1;
            viterbiCounter++;
        }
    }

    if (PI3!= NULL)
        for (i = 0; i < L3; i++) {
            for (j = 0; j < 128; j++) {
                if (PI3[j % 32] != 0)
                    protection->indexTable[viterbiCounter] = 1;
                viterbiCounter++;
            }
        }

    if (PI4 != NULL)
        for (i = 0; i < L4; i++) {
            for (j = 0; j < 128; j++) {
                if (PI4[j % 32] != 0)
                    protection->indexTable[viterbiCounter] = 1;
                viterbiCounter++;
            }
        }


    PI_X = get_PCodes(8 - 1);

    //	we had a final block of 24 bits  with puncturing according to PI_X
    //	This block constitues the 6 * 4 bits of the register itself.
    for (i = 0; i < 24; i++) {
        if (PI_X[i] != 0)
            protection->indexTable[viterbiCounter] = 1;
        viterbiCounter++;
    }
}

void protection_destroy(protection_t* protection) {
    free(protection->disperseVector);
    free(protection->viterbiBlock);
    free(protection->indexTable);
    destroyViterbi768(&protection->vp);
    free(protection);
}

void protection_deconvolve(protection_t *protection, int16_t *v, uint8_t *outBuffer) {
int16_t	i;
int16_t	inputCounter	= 0;

	memset (protection->viterbiBlock, 0, protection->indexTableSize * sizeof (int16_t)); 


        for (i = 0; i < protection->indexTableSize; i ++) {
           if (protection->indexTable [i]) {
              protection->viterbiBlock [i] = v [inputCounter ++];
           }
        }

///     The actual deconvolution is done by the viterbi decoder
	deconvolve (&protection->vp, protection->viterbiBlock, outBuffer);
	
//
//      and the energy dispersal
        for (i = 0; i < protection->outSize; i ++) {
           outBuffer [i] ^= protection->disperseVector [i];
        }
}
