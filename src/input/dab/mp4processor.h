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

#ifndef _MP4PROCESSOR_H
#define _MP4PROCESSOR_H

#include "tvheadend.h"

#include "rtlsdr/reed-solomon.h"

typedef struct mp4processor mp4processor_t;

struct mp4processor {
	reedSolomon_t	*my_rsDecoder;
	uint8_t		*outVector;

	int16_t		blockFillIndex;
	int16_t		blocksInBuffer;
	int16_t		blockCount;
	int16_t		bitRate;
	uint8_t		*frameBytes;
	int16_t		RSDims;
	int16_t		au_start	[10];

	int16_t		frameCount;
	int16_t		successFrames;
	int16_t		frameErrors;
	int16_t		rsErrors;

	int16_t		frame_quality;
	int16_t		rs_quality;

	FILE 		*pFile;
	int header;
};

mp4processor_t* init_mp4processor(int16_t bitRate);
void destroy_mp4processor(mp4processor_t* mp4processor);
void	mp4Processor_addtoFrame (mp4processor_t* mp4processor, uint8_t *V);

#endif
