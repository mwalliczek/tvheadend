/*
 *    Copyright (C) 2014 .. 2017
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
 *    You should have received a copy of the GNU General Public License
 *    along with DAB-library; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * 	superframer for the SDR-J DAB+ receiver (and derived programs)
 * 	This processor handles the whole DAB+ specific part
 ************************************************************************
 *	may 15 2015. A real improvement on the code
 *	is the addition from Stefan Poeschel to create a
 *	header for the aac that matches, really a big help!!!!
 ************************************************************************
 *
 *       Adapted to tvheadend by Matthias Walliczek (matthias@walliczek.de)
 */
#include "mp4processor.h"
#include "rtlsdr/firecheck.h"
#include "rtlsdr/reed-solomon.h"

static inline
int	check_crc_bytes (uint8_t *msg, int32_t len) {
int i, j;
uint16_t	accumulator	= 0xFFFF;
uint16_t	crc;
uint16_t	genpoly		= 0x1021;

	for (i = 0; i < len; i ++) {
	   int16_t data = msg [i] << 8;
	   for (j = 8; j > 0; j--) {
	      if ((data ^ accumulator) & 0x8000)
	         accumulator = ((accumulator << 1) ^ genpoly) & 0xFFFF;
	      else
	         accumulator = (accumulator << 1) & 0xFFFF;
	      data = (data << 1) & 0xFFFF;
	   }
	}
//
//	ok, now check with the crc that is contained
//	in the au
	crc	= ~((msg [len] << 8) | msg [len + 1]) & 0xFFFF;
	return (crc ^ accumulator) == 0;
}

typedef struct {
        int rfa;
        int dacRate;
        int sbrFlag;
        int psFlag;
        int aacChannelMode;
        int mpegSurround;
} stream_parms;

mp4processor_t* init_mp4processor(int16_t bitRate) {
	mp4processor_t* res = calloc(1, sizeof(mp4processor_t));
	
	res->my_rsDecoder = init_reedSolomon(8, 0435, 0, 1, 10);
	
	res->bitRate		= bitRate;
	
	res->RSDims		= bitRate / 8;
	res->frameBytes		= calloc(res->RSDims * 120, sizeof(uint8_t));	// input
	res->outVector		= calloc(res->RSDims * 110, sizeof(uint8_t));
	res->blockFillIndex	= 0;
	res->blocksInBuffer	= 0;

	res->frameCount      = 0;
        res->frameErrors     = 0;
        res->successFrames   = 0;
        res->rsErrors        = 0;

	res->frame_quality	= 0;
	res->rs_quality	= 0;
	
	res->pFile = fopen ("/tmp/temp.mp4", "wb");
	
	res->header = 0;
	
	return res;
}

void destroy_mp4processor(mp4processor_t* mp4processor) {
	destroy_reedSolomon(mp4processor->my_rsDecoder);
	fclose(mp4processor->pFile);
	free(mp4processor->outVector);
	free(mp4processor->frameBytes);
	free(mp4processor);
}

int	mp4Processor_processSuperframe (mp4processor_t* mp4processor, uint8_t frameBytes [],
	                                 int16_t base);

void	mp4Processor_buildHeader (int16_t framelen,
	                           stream_parms *sp,
	                           uint8_t *header);

void    mp4Processor_addtoFrame (mp4processor_t* mp4processor, uint8_t *V) {
int16_t	i, j;
uint8_t	temp	= 0;
int16_t	nbits	= 24 * mp4processor->bitRate;
//
//	Note that the packing in the entry vector is still one bit
//	per Byte, nbits is the number of Bits (i.e. containing bytes)

	tvhtrace(LS_RTLSDR, "mp4Processor_addtoFrame");
	
	for (i = 0; i < nbits / 8; i ++) {	// in bytes
	   temp = 0;
	   for (j = 0; j < 8; j ++)
	      temp = (temp << 1) | (V [i * 8 + j] & 01);
	   mp4processor->frameBytes [mp4processor->blockFillIndex * nbits / 8 + i] = temp;
	}
//
	mp4processor->blocksInBuffer ++;
	mp4processor->blockFillIndex = (mp4processor->blockFillIndex + 1) % 5;
//
//	we take the last five blocks to look at
	if (mp4processor->blocksInBuffer >= 5) {
	   if (++mp4processor->frameCount >= 50) {
	      mp4processor->frameCount = 0;
	      mp4processor->frame_quality	= 2 * (50 - mp4processor->frameErrors);
	      tvhtrace(LS_RTLSDR, "frame_quality: %d", mp4processor->frame_quality);
//	      if (mp4processor->mscQuality != NULL)
//	         mscQuality (frame_quality, rs_quality, aac_quality, ctx);
	      mp4processor->frameErrors = 0;
	   }

//	OK, we give it a try, check the fire code
	   if (firecode_check(&mp4processor->frameBytes [mp4processor->blockFillIndex * nbits / 8]) &&
	       (mp4Processor_processSuperframe (mp4processor, mp4processor->frameBytes,
	                           mp4processor->blockFillIndex * nbits / 8))) {
//	since we processed a full cycle of 5 blocks, we just start a
//	new sequence, beginning with block blockFillIndex
	     mp4processor-> blocksInBuffer	= 0;
	      if (++mp4processor->successFrames > 25) {
	         mp4processor->rs_quality	= 4 * (25 - mp4processor->rsErrors);
                 mp4processor->successFrames  = 0;
                 mp4processor->rsErrors       = 0;
              }
	   }
	   else {	// virtual shift to left in block sizes
	      mp4processor->blocksInBuffer  = 4;
	      mp4processor->frameErrors ++;
	      tvhtrace(LS_RTLSDR, "frameErrors++");
	   }
	}
}

int	mp4Processor_processSuperframe (mp4processor_t* mp4processor, uint8_t frameBytes [],
	                                 int16_t base) {
uint8_t		num_aus;
int16_t		i, j, k;
uint8_t		rsIn	[120];
uint8_t		rsOut	[110];
stream_parms	streamParameters;

/**	apply reed-solomon error repar
  *	OK, what we now have is a vector with RSDims * 120 uint8_t's
  *	Output is a vector with RSDims * 110 uint8_t's
  */
  	tvhtrace(LS_RTLSDR, "mp4Processor_processSuperframe");
	for (j = 0; j < mp4processor->RSDims; j ++) {
	   int16_t ler	= 0;
	   for (k = 0; k < 120; k ++) 
	      rsIn [k] = frameBytes [(base + j + k * mp4processor->RSDims) % (mp4processor->RSDims * 120)];
//
	   ler = reedSolomon_dec (mp4processor->my_rsDecoder, rsIn, rsOut, 135);
	   if (ler < 0)
	      return 0;
	   for (k = 0; k < 110; k ++) 
	      mp4processor->outVector [j + k * mp4processor->RSDims] = rsOut [k];
	}
//
//	OK, the result is N * 110 * 8 bits 
//	bits 0 .. 15 is firecode
//	bit 16 is unused
	streamParameters. dacRate = (mp4processor->outVector [2] >> 6) & 01;	// bit 17
	streamParameters. sbrFlag = (mp4processor->outVector [2] >> 5) & 01;	// bit 18
	streamParameters. aacChannelMode = (mp4processor->outVector [2] >> 4) & 01;	// bit 19
	streamParameters. psFlag   = (mp4processor->outVector [2] >> 3) & 01;	// bit 20
	streamParameters. mpegSurround	= (mp4processor->outVector [2] & 07);	// bits 21 .. 23

	switch (2 * streamParameters. dacRate + streamParameters. sbrFlag) {
	   default:		// cannot happen
	   case 0:
	      num_aus = 4;
	      mp4processor->au_start [0] = 8;
	      mp4processor->au_start [1] = mp4processor->outVector [3] * 16 + (mp4processor->outVector [4] >> 4);
	      mp4processor->au_start [2] = (mp4processor->outVector [4] & 0xf) * 256 + mp4processor->outVector [5];
	      mp4processor->au_start [3] = mp4processor->outVector [6] * 16 + (mp4processor->outVector [7] >> 4);
	      mp4processor->au_start [4] = 110 *  (mp4processor->bitRate / 8);
	      break;
//
	   case 1:
	      num_aus = 2;
	      mp4processor->au_start [0] = 5;
	      mp4processor->au_start [1] = mp4processor->outVector [3] * 16 + (mp4processor->outVector [4] >> 4);
	      mp4processor->au_start [2] = 110 * (mp4processor->bitRate / 8);
	      break;
//
	   case 2:
	      num_aus = 6;
	      mp4processor->au_start [0] = 11;
	      mp4processor->au_start [1] = mp4processor->outVector [3] * 16 + (mp4processor->outVector [4] >> 4);
	      mp4processor->au_start [2] = (mp4processor->outVector [4] & 0xf) * 256 + mp4processor->outVector [5];
	      mp4processor->au_start [3] = mp4processor->outVector [6] * 16 + (mp4processor->outVector [7] >> 4);
	      mp4processor->au_start [4] = (mp4processor->outVector [7] & 0xf) * 256 +
	                     mp4processor->outVector [8];
	      mp4processor->au_start [5] = mp4processor->outVector [9] * 16 +
	                     (mp4processor->outVector [10] >> 4);
	      mp4processor->au_start [6] = 110 *  (mp4processor->bitRate / 8);
	      break;
//
	   case 3:
	      num_aus = 3;
	      mp4processor->au_start [0] = 6;
	      mp4processor->au_start [1] = mp4processor->outVector [3] * 16 + (mp4processor->outVector [4] >> 4);
	      mp4processor->au_start [2] = (mp4processor->outVector [4] & 0xf) * 256 +
	                     mp4processor->outVector [5];
	      mp4processor->au_start [3] = 110 * (mp4processor->bitRate / 8);
	      break;
	}
//
//	extract the AU'si and prepare a buffer, sufficiently
//	long for conversion to PCM samples

	for (i = 0; i < num_aus; i ++) {
	   int16_t	aac_frame_length;
//
	   if (mp4processor->au_start [i + 1] < mp4processor->au_start [i]) {
//	      fprintf (stderr, "%d %d\n", mp4processor->au_start [i + 1], mp4processor->au_start [i]);
	      return 0;
	   }

	   aac_frame_length = mp4processor->au_start [i + 1] - mp4processor->au_start [i] - 2;
// sanity check
	   if ((aac_frame_length >= 960) || (aac_frame_length < 0)) {
	      return 0;
	   }

//	but first the crc check
	   if (check_crc_bytes (&mp4processor->outVector [mp4processor->au_start [i]],
	                        aac_frame_length)) {
//
//	if there is pad handle it always
	      if (((mp4processor->outVector [mp4processor->au_start [i] + 0] >> 5) & 07) == 4) {
	         int16_t count = mp4processor->outVector [mp4processor->au_start [i] + 1];
                 uint8_t buffer [count];
                 memcpy (buffer, &mp4processor->outVector [mp4processor->au_start [i] + 2], count);
//                 uint8_t L0   = buffer [count - 1];
//                 uint8_t L1   = buffer [count - 2];
//                 my_padHandler. processPAD (buffer, count - 3, L1, L0);
              }
	      uint8_t fileBuffer [1024];
	      mp4Processor_buildHeader (aac_frame_length, &streamParameters, fileBuffer);
	      memcpy (&fileBuffer [7], 
	              &mp4processor->outVector [mp4processor->au_start [i]],
	              aac_frame_length);
	      if (!mp4processor->header) {
		      fwrite (&fileBuffer [0], 1, aac_frame_length + 7, mp4processor->pFile);
		      mp4processor->header = 1;
	      } else {
		      fwrite (&fileBuffer [7], 1, aac_frame_length, mp4processor->pFile);
	      }
//	      if (soundOut != NULL) 
//	         (soundOut)((int16_t *)(&fileBuffer [0]),
//	                    aac_frame_length + 7, 0, false, NULL);
	   }
	   else {
	      fprintf (stderr, "CRC failure with dab+ frame should not happen\n");
	   }
	}
	return 1;
}

void	mp4Processor_buildHeader (int16_t framelen,
	                           stream_parms *sp,
	                           uint8_t *header) {
	struct adts_fixed_header {
		unsigned                     : 4;
		unsigned home                : 1;
		unsigned orig                : 1;
		unsigned channel_config      : 3;
		unsigned private_bit         : 1;
		unsigned sampling_freq_index : 4;
		unsigned profile             : 2;
		unsigned protection_absent   : 1;
		unsigned layer               : 2;
		unsigned id                  : 1;
		unsigned syncword            : 12;
	} fh;
	struct adts_variable_header {
		unsigned                            : 4;
		unsigned num_raw_data_blks_in_frame : 2;
		unsigned adts_buffer_fullness       : 11;
		unsigned frame_length               : 13;
		unsigned copyright_id_start         : 1;
		unsigned copyright_id_bit           : 1;
	} vh;
	/* 32k 16k 48k 24k */
	const unsigned short samptab[] = {0x5, 0x8, 0x3, 0x6};

	fh. syncword = 0xfff;
	fh. id = 0;
	fh. layer = 0;
	fh. protection_absent = 1;
	fh. profile = 0;
	fh. sampling_freq_index = samptab [sp -> dacRate << 1 | sp -> sbrFlag];

	fh. private_bit = 0;
	switch (sp -> mpegSurround) {
	   default:
	      fprintf (stderr, "Unrecognized mpeg_surround_config ignored\n");
//	not nice, but deliberate: fall through
	   case 0:
	      if (sp -> sbrFlag && !sp -> aacChannelMode && sp -> psFlag)
	         fh. channel_config = 2; /* Parametric stereo */
	      else
	         fh. channel_config = 1 << sp -> aacChannelMode ;
	      break;

	   case 1:
	      fh. channel_config = 6;
	      break;
	}

	fh. orig = 0;
	fh. home = 0;
	vh. copyright_id_bit = 0;
	vh. copyright_id_start = 0;
	vh. frame_length = framelen + 7;  /* Includes header length */
	vh. adts_buffer_fullness = 1999;
	vh. num_raw_data_blks_in_frame = 0;
	header [0]	= fh. syncword >> 4;
	header [1]	= (fh. syncword & 0xf) << 4;
	header [1]	|= fh. id << 3;
	header [1]	|= fh. layer << 1;
	header [1]	|= fh. protection_absent;
        header [2]	= fh. profile << 6;
	header [2]	|= fh. sampling_freq_index << 2;
	header [2]	|= fh. private_bit << 1;
	header [2]	|= (fh. channel_config & 0x4);
	header [3]	= (fh. channel_config & 0x3) << 6;
	header [3]	|= fh. orig << 5;
	header [3]	|= fh. home << 4;
	header [3]	|= vh. copyright_id_bit << 3;
	header [3]	|= vh. copyright_id_start << 2;
	header [3]	|= (vh. frame_length >> 11) & 0x3;
	header [4]	= (vh. frame_length >> 3) & 0xff;
	header [5]	= (vh. frame_length & 0x7) << 5;
	header [5]	|= vh. adts_buffer_fullness >> 6;
	header [6]	= (vh. adts_buffer_fullness & 0x3f) << 2;
	header [6]	|= vh. num_raw_data_blks_in_frame;
}


