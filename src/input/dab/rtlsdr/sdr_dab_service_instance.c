/*
 *  Tvheadend - RTL SDR Input System
 *
 *  Copyright (C) 2019 Matthias Walliczek
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "dab.h"
#include "tvheadend.h"

sdr_dab_service_instance_t * 
sdr_dab_service_instance_create(dab_service_t* service)
{
    int i, j;
    uint8_t	shiftRegister [9];
    sdr_dab_service_instance_t* res = calloc(1, sizeof(sdr_dab_service_instance_t));
    memset(res, 0, sizeof(sdr_dab_service_instance_t));
    res->dai_service = service;
    res->subChannel = &service->s_dab_ensemble->subChannels[service->subChId];
    
    res->outV = calloc(24 * res->subChannel -> BitRate, sizeof (uint8_t));
    
    res->fragmentSize = res->subChannel->Length * CUSize;
    
    for (i = 0; i < 16; i ++) {
        res->interleaveData [i] = calloc(res->fragmentSize, sizeof (int16_t));
	memset (res->interleaveData [i], 0, res->fragmentSize * sizeof (int16_t));
    }
    
    res->interleaverIndex	= 0;
    res->countforInterleaver	= 0;

    if (res->subChannel->shortForm)
	res->protection	= uep_protection_init(res->subChannel->BitRate,
	                                              res->subChannel->protLevel);
    else
	res->protection	= eep_protection_init(res->subChannel->BitRate,
	                                              res->subChannel->protLevel);

    res->mp4processor = init_mp4processor(res->subChannel->BitRate);

    res->tempX = calloc(res->fragmentSize, sizeof(int16_t));
    res->nextIn			= 0;
    res->nextOut		= 0;
    for (i = 0; i < 20; i ++)
        res->theData [i] = calloc(res->fragmentSize, sizeof (int16_t));

    res->disperseVector = calloc(res->subChannel->BitRate * 24, sizeof (uint8_t));
    memset (shiftRegister, 1, 9);
    for (i = 0; i < res->subChannel->BitRate * 24; i ++) {
        uint8_t b = shiftRegister [8] ^ shiftRegister [4];
        for (j = 8; j > 0; j--)
            shiftRegister [j] = shiftRegister [j - 1];
        shiftRegister [0] = b;
        res->disperseVector [i] = b;
    }
    


    return res;
}

void sdr_dab_service_instance_destroy(sdr_dab_service_instance_t* sds) {
    int i;
    
    free(sds->tempX);
    free(sds->disperseVector);
    free(sds->outV);
    for (i = 0; i < 16; i ++)
        free(sds->interleaveData [i]);
    for (i = 0; i < 20; i ++)
        free(sds->theData [i]);
    protection_destroy(sds->protection);
    destroy_mp4processor(sds->mp4processor);
    free(sds);
}

const	int16_t interleaveMap [] = {0,8,4,12,2,10,6,14,1,9,5,13,3,11,7,15};

void    processSegment (sdr_dab_service_instance_t *sds, int16_t *Data);

void    processSegment (sdr_dab_service_instance_t *sds, int16_t *Data) {
    int16_t i;

    for (i = 0; i < sds->fragmentSize; i ++) {
        sds->tempX [i] = sds->interleaveData [(sds->interleaverIndex +
                                     interleaveMap [i & 017]) & 017][i];
        sds->interleaveData [sds->interleaverIndex][i] = Data [i];
    }

    sds->interleaverIndex = (sds->interleaverIndex + 1) & 0x0F;
    
//  only continue when de-interleaver is filled
    if (sds->countforInterleaver <= 15) {
        sds->countforInterleaver ++;
        return;
    }
    
    protection_deconvolve(sds->protection, sds->tempX, sds->fragmentSize,
                                        sds->outV);
                                        
//
//      and the energy dispersal
    for (i = 0; i < sds->subChannel->BitRate * 24; i ++)
	sds->outV [i] ^= sds->disperseVector [i];
	
    mp4Processor_addtoFrame(sds->mp4processor, sds->outV);

}

void
sdr_dab_service_instance_process_data(sdr_dab_service_instance_t *sds, int16_t *v, int16_t cnt)
{
    memcpy (sds->theData [sds->nextIn], v, sds->fragmentSize * sizeof (int16_t));
    processSegment (sds, sds->theData [sds->nextIn]);
    sds->nextIn = (sds->nextIn + 1) % 20;
    
}
