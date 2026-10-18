/*
*    Copyright (C) 2013 .. 2017
*    Jan van Katwijk (J.vanKatwijk@gmail.com)
*    Lazy Chair Computing
*
*    Derived from dab-cmdline, https://github.com/JvanKatwijk/dab-cmdline
*    (library/src/backend/msc-handler.cpp)
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

/* a sub-channel which fits into the CIF (864 CU) */
static int subchannel_valid(const subChannel *sc) {
    return sc->Length > 0 && sc->StartAddr >= 0 &&
           sc->StartAddr + sc->Length <= numberofblocksperCIF * 2 * K / CUSize;
}

/*
 * The symbols (bit 0 .. 17 of a CIF) which carry a part of an active
 * sub-channel. A symbol carries 2 * K bits, that is 48 CU, so a 72 kbit/s
 * EEP-3A sub-channel (54 CU) needs 2 or 3 of the 18 symbols.
 */
uint32_t msc_symbol_mask(struct sdr_state_t *sdr) {
    sdr_dab_service_instance_t *s;
    uint32_t mask = 0;
    int first, last;

    tvh_mutex_lock(&sdr->active_service_mutex);
    LIST_FOREACH(s, &sdr->active_service_instance, service_link) {
        if (!subchannel_valid(s->subChannel))
            continue;
        first = s->subChannel->StartAddr * CUSize / (2 * K);
        last = ((s->subChannel->StartAddr + s->subChannel->Length) * CUSize - 1) / (2 * K);
        for (; first <= last; first++)
            mask |= 1u << first;
    }
    tvh_mutex_unlock(&sdr->active_service_mutex);
    return mask;
}

/* data is NULL for a symbol no active sub-channel uses (not demodulated) */
void process_mscBlock(struct sdr_state_t *sdr, const int16_t data[], int16_t blkno) {
    int16_t	currentblk;
    sdr_dab_service_instance_t* s;

    currentblk	= (blkno - 4) % numberofblocksperCIF;
    if (data)
        memcpy (&sdr->cifVector [currentblk * 2 * K], data, 2 * K * sizeof (int16_t));
    if (currentblk < numberofblocksperCIF - 1) 
        return;

    // OK, now we have a full CIF
    
    tvh_mutex_lock(&sdr->active_service_mutex);
    
    LIST_FOREACH(s, &sdr->active_service_instance, service_link) {
	if (subchannel_valid(s->subChannel)) {
            tvhtrace(LS_RTLSDR, "msc -> %s", s->dai_service ? s->dai_service->s_nicename : "data");

            sdr_dab_service_instance_process_data(s,
                &sdr->cifVector[s->subChannel->StartAddr * CUSize]);
            sdr_dab_service_instance_stats(s, sdr);
        }
    }

    tvh_mutex_unlock(&sdr->active_service_mutex);
}