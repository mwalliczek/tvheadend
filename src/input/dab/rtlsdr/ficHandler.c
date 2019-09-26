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
#include "fib-processor.h"
#include "viterbi_768/viterbi-768.h"

uint8_t		PRBS[768];
uint8_t		shiftRegister[9];
uint8_t		punctureTable[4 * 768 + 24];

void process_ficInput(struct sdr_state_t *sdr, int16_t ficno);

void initFicHandler(struct sdr_state_t *sdr) {
	int16_t i, j, k;
	int16_t	local = 0;

	sdr->index = 0;
	sdr->BitsperBlock = 2 * 1536;
	sdr->ficno = 0;
	sdr->fibCRCsuccess = 0;
	sdr->fibCRCtotal = 0;
	
	memset(shiftRegister, 1, 9);

	for (i = 0; i < 768; i++) {
		PRBS[i] = shiftRegister[8] ^ shiftRegister[4];
		for (j = 8; j > 0; j--)
			shiftRegister[j] = shiftRegister[j - 1];

		shiftRegister[0] = PRBS[i];
	}
	/**
	*	a block of 2304 bits is considered to be a codeword
	*	In the first step we have 21 blocks with puncturing according to PI_16
	*	each 128 bit block contains 4 subblocks of 32 bits
	*	on which the given puncturing is applied
	*/
	memset(punctureTable, 0, (3072 + 24) * sizeof(uint8_t));

	for (i = 0; i < 21; i++) {
		for (k = 0; k < 32 * 4; k++) {
			if (get_PCodes(16 - 1)[k % 32] == 1)
				punctureTable[local] = 1;
			local++;
		}
	}
	/**
	*	In the second step
	*	we have 3 blocks with puncturing according to PI_15
	*	each 128 bit block contains 4 subblocks of 32 bits
	*	on which the given puncturing is applied
	*/
	for (i = 0; i < 3; i++) {
		for (k = 0; k < 32 * 4; k++) {
			if (get_PCodes(15 - 1)[k % 32] == 1)
				punctureTable[local] = 1;
			local++;
		}
	}

	/**
	*	we have a final block of 24 bits  with puncturing according to PI_X
	*	This block constitues the 6 * 4 bits of the register itself.
	*/
	for (k = 0; k < 24; k++) {
		if (get_PCodes(8 - 1)[k] == 1)
			punctureTable[local] = 1;
		local++;
	}
	initViterbi768(&sdr->vp, 768, 1);
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
	int16_t	viterbiBlock[3072 + 24];
	int16_t	inputCount = 0;

	tvhtrace(LS_RTLSDR, "process_ficInput started: %d", ficno);
	memset(viterbiBlock, 0, (3072 + 24) * sizeof(int16_t));

	for (i = 0; i < 4 * 768 + 24; i++)
		if (punctureTable[i])
			viterbiBlock[i] = sdr->ofdm_input[inputCount++];
	/**
	*	Now we have the full word ready for deconvolution
	*	deconvolution is according to DAB standard section 11.2
	*/
	deconvolve(&sdr->vp, viterbiBlock, sdr->bitBuffer_out);
	/**
	*	if everything worked as planned, we now have a
	*	768 bit vector containing three FIB's
	*
	*	first step: energy dispersal according to the DAB standard
	*	We use a predefined vector PRBS
	*/
	for (i = 0; i < 768; i++)
		sdr->bitBuffer_out[i] ^= PRBS[i];
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
		process_FIB(sdr, p, ficno);
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
