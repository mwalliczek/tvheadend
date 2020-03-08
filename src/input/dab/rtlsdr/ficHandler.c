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
#include "ficHandler.h"
#include "protTables.h"
#include "../fib-processor.h"

void process_ficInput(struct sdr_state_t *sdr, int16_t ficno);

void initFicHandler(struct sdr_state_t *sdr) {
	sdr->index = 0;
	sdr->BitsperBlock = 2 * 1536;
	sdr->ficno = 0;
	sdr->fibCRCsuccess = 0;
	sdr->fibCRCtotal = 0;
	sdr->protection = fic_protection_init();
}

void destroyFicHandler(struct sdr_state_t *sdr) {
	protection_destroy(sdr->protection);
}

void process_ficBlock(struct sdr_state_t *sdr, int16_t data[], int16_t blkno) {
	int32_t	i;

	if (blkno == 1) {
		sdr->index = 0;
		sdr->ficno = 0;
	}
	//
	if ((1 <= blkno) && (blkno <= 3)) {
		for (i = 0; i < sdr->BitsperBlock; i++) {
			sdr->ofdm_input[sdr->index++] = data[i];
			if (sdr->index >= 2304) {
				process_ficInput(sdr, sdr->ficno);
				sdr->index = 0;
				sdr->ficno++;
			}
		}
	}
	else
		fprintf(stderr, "You should not call ficBlock here\n");
	//	we are pretty sure now that after block 4, we end up
	//	with index = 0
}

void process_ficInput(struct sdr_state_t *sdr, int16_t ficno) {
	int16_t	i;

	tvhtrace(LS_RTLSDR, "process_ficInput started: %d", ficno);
	protection_deconvolve(sdr->protection, sdr->ofdm_input, sdr->bitBuffer_out);

	/**
	*	each of the fib blocks is protected by a crc
	*	(we know that there are three fib blocks each time we are here
	*	we keep track of the successrate
	*	and show that per 100 fic blocks
	*/
	for (i = ficno * 3; i < ficno * 3 + 3; i++) {
		uint8_t *p = &sdr->bitBuffer_out[(i % 3) * 256];
		if (sdr->fibCRCtotal < 100) {
			sdr->fibCRCtotal++;
		} else {
			tvhdebug(LS_RTLSDR, "ficHandler crc rate %d", sdr->fibCRCsuccess - 1);
			sdr->fibCRCsuccess = 0;
			sdr->fibCRCtotal = 0;
		}
		if (!check_CRC_bits(p, 256)) {
			continue;
		}
		tvhtrace(LS_RTLSDR, "ficHandler checkCRC success");
		process_FIB(sdr->mmi, p, ficno);
		sdr->fibCRCsuccess++;
#ifdef TRACE_FIC_HANDLER
	        printf("i: %d\n", i);
	        FILE *pFile = fopen ("/tmp/ficInput", "wb");
	        fwrite (sdr->ofdm_input, 2, 2304, pFile);
	        fclose (pFile);
	        pFile = fopen ("/tmp/ficOutput", "wb");
	        fwrite (p, 1, 256, pFile);
	        fclose (pFile);
	        exit(0);
#endif
	}
}
