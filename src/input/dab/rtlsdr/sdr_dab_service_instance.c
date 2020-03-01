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
    
    res->fragmentSize = res->subChannel->Length * CUSize;
    
    for (i = 0; i < 16; i ++) {
        res->interleaveData [i] = calloc(1, res->fragmentSize * sizeof (int16_t));
	memset (res->interleaveData [i], 0, res->fragmentSize * sizeof (int16_t));
    }
    
    res->nextIn			= 0;
    res->nextOut		= 0;
    for (i = 0; i < 20; i ++)
        res->theData [i] = calloc(1, sizeof (int16_t) * res->fragmentSize);

    res->disperseVector = calloc(1, sizeof (uint8_t) * res->subChannel->BitRate * 24);
    memset (shiftRegister, 1, 9);
    for (i = 0; i < res->subChannel->BitRate * 24; i ++) {
        uint8_t b = shiftRegister [8] ^ shiftRegister [4];
        for (j = 8; j > 0; j--) {
            shiftRegister [j] = shiftRegister [j - 1];
            shiftRegister [0] = b;
            res->disperseVector [i] = b;
        }
    }
    
    return res;
}

const	int16_t interleaveMap [] = {0,8,4,12,2,10,6,14,1,9,5,13,3,11,7,15};

void
sdr_dab_service_instance_process_data(sdr_dab_service_instance_t *sds, int16_t *v, int16_t cnt)
{
    memcpy (sds->theData [sds->nextIn], v, sds->fragmentSize * sizeof (int16_t));
    sds->nextIn = (sds->nextIn + 1) % 20;
    
}
