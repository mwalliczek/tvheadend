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
*	Adapted to tvheadend by Matthias Walliczek (matthias@walliczek.de)
*/

#include <string.h>
#include <math.h>

#include "dab.h"
#include "tvheadend.h"
#include "mscHandler.h"

void process_mscBlock(struct sdr_state_t *sdr, int16_t data[], int16_t blkno) {
    int16_t	currentblk;
    sdr_dab_service_instance_t* s;

    currentblk	= (blkno - 4) % numberofblocksperCIF;
    memcpy (&sdr->cifVector [currentblk * 2 * K], data, 2 * K * sizeof (int16_t));
    if (currentblk < numberofblocksperCIF - 1) 
        return;

    // OK, now we have a full CIF
    tvhtrace(LS_RTLSDR, "OK, now we have a full CIF");
    
    tvh_mutex_lock(&sdr->active_service_mutex);
    
    LIST_FOREACH(s, &sdr->active_service_instance, service_link) {
        tvhtrace(LS_RTLSDR, "checking %s", s->dai_service->s_nicename);
	int startAddr	= s -> subChannel -> StartAddr;
	int Length	= s -> subChannel -> Length;
	if (Length > 0) {
            int16_t myBegin [Length * CUSize];
	    memcpy (myBegin, &sdr->cifVector [startAddr * CUSize],
	                               Length * CUSize * sizeof (int16_t));
            tvhtrace(LS_RTLSDR, "msc -> %s", s->dai_service->s_nicename);

            sdr_dab_service_instance_process_data(s, myBegin);
        }
    }

    tvh_mutex_unlock(&sdr->active_service_mutex);
}