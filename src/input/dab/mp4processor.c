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
int	check_crc_bytes(const uint8_t *msg, int32_t len) {
    int i, j;
    uint16_t	accumulator = 0xFFFF;
    uint16_t	crc;
    uint16_t	genpoly = 0x1021;

    for (i = 0; i < len; i++) {
        int16_t data = msg[i] << 8;
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
    crc = ~((msg[len] << 8) | msg[len + 1]) & 0xFFFF;
    return (crc ^ accumulator) == 0;
}

mp4processor_t* init_mp4processor(int16_t bitRate, void *context, void(*writeCb)(const uint8_t*, int16_t, 
        const stream_parms* stream_parms, void*)) {
    mp4processor_t* res = calloc(1, sizeof(mp4processor_t));

    res->my_rsDecoder = init_reedSolomon(8, 0435, 0, 1, 10);

    res->bitRate = bitRate;

    res->RSDims = bitRate / 8;
    res->frameBytes = calloc(res->RSDims * 120, sizeof(uint8_t));	// input
    res->outVector = calloc(res->RSDims * 110, sizeof(uint8_t));
    res->blockFillIndex = 0;
    res->blocksInBuffer = 0;

    res->frameCount = 0;
    res->frameErrors = 0;
    res->successFrames = 0;
    res->rsErrors = 0;

    res->frame_quality = 0;
    res->rs_quality = 0;

    res->context = context;
    res->writeCb = writeCb;

    return res;
}

void destroy_mp4processor(mp4processor_t* mp4processor) {
    destroy_reedSolomon(mp4processor->my_rsDecoder);
    free(mp4processor->outVector);
    free(mp4processor->frameBytes);
    free(mp4processor);
}

int mp4Processor_processSuperframe(mp4processor_t* mp4processor, const uint8_t frameBytes[],
    int16_t base);

int16_t mp4Processor_writeFrame(int16_t framelen,
    const stream_parms *sp,
    uint8_t *output, uint8_t *data);

#ifdef TRACE_MP4
int counter = 0;
#endif

void mp4Processor_addtoFrame(mp4processor_t* mp4processor, const uint8_t *V) {
    int16_t	i, j;
    uint8_t	temp = 0;
    int16_t	nbits = 24 * mp4processor->bitRate;

#ifdef TRACE_MP4
    char buffer[128];
    snprintf(buffer, 128, "/tmp/mp4Input%d", counter++);
    FILE *pFile = fopen(buffer, "wb");
    fwrite(V, 1, nbits, pFile);
    fclose(pFile);
#endif

    //
    //	Note that the packing in the entry vector is still one bit
    //	per Byte, nbits is the number of Bits (i.e. containing bytes)

    tvhtrace(LS_RTLSDR, "mp4Processor_addtoFrame");


    for (i = 0; i < nbits / 8; i++) {	// in bytes
        temp = 0;
        for (j = 0; j < 8; j++)
            temp = (temp << 1) | (V[i * 8 + j] & 01);
        mp4processor->frameBytes[mp4processor->blockFillIndex * nbits / 8 + i] = temp;
    }
    //
    mp4processor->blocksInBuffer++;
    mp4processor->blockFillIndex = (mp4processor->blockFillIndex + 1) % 5;
    //
    //	we take the last five blocks to look at
    if (mp4processor->blocksInBuffer >= 5) {
        if (++mp4processor->frameCount >= 50) {
            mp4processor->frameCount = 0;
            mp4processor->frame_quality = 2 * (50 - mp4processor->frameErrors);
            tvhtrace(LS_RTLSDR, "frame_quality: %d", mp4processor->frame_quality);
            //	      if (mp4processor->mscQuality != NULL)
            //	         mscQuality (frame_quality, rs_quality, aac_quality, ctx);
            mp4processor->frameErrors = 0;
        }

        //	OK, we give it a try, check the fire code
        if (firecode_check(&mp4processor->frameBytes[mp4processor->blockFillIndex * nbits / 8]) &&
            (mp4Processor_processSuperframe(mp4processor, mp4processor->frameBytes,
                mp4processor->blockFillIndex * nbits / 8))) {
            //	since we processed a full cycle of 5 blocks, we just start a
            //	new sequence, beginning with block blockFillIndex
            mp4processor->blocksInBuffer = 0;
            if (++mp4processor->successFrames > 25) {
                mp4processor->rs_quality = 4 * (25 - mp4processor->rsErrors);
                mp4processor->successFrames = 0;
                mp4processor->rsErrors = 0;
            }
        } else {	// virtual shift to left in block sizes
            mp4processor->blocksInBuffer = 4;
            mp4processor->frameErrors++;
            tvhtrace(LS_RTLSDR, "frameErrors++");
        }
    }
}

int	mp4Processor_processSuperframe(mp4processor_t* mp4processor, const uint8_t frameBytes[],
    int16_t base) {
    uint8_t		num_aus;
    int16_t		i, j, k;
    uint8_t		rsIn[120];
    uint8_t		rsOut[110];
    stream_parms	streamParameters;

    /**	apply reed-solomon error repar
      *	OK, what we now have is a vector with RSDims * 120 uint8_t's
      *	Output is a vector with RSDims * 110 uint8_t's
      */
    tvhtrace(LS_RTLSDR, "mp4Processor_processSuperframe");
    for (j = 0; j < mp4processor->RSDims; j++) {
        int16_t ler = 0;
        for (k = 0; k < 120; k++)
            rsIn[k] = frameBytes[(base + j + k * mp4processor->RSDims) % (mp4processor->RSDims * 120)];
        //
        ler = reedSolomon_dec(mp4processor->my_rsDecoder, rsIn, rsOut, 135);
        if (ler < 0) {
            tvhdebug(LS_RTLSDR, "reedSolomon_dec %d < 0", ler);
            return 0;
        }
        for (k = 0; k < 110; k++)
            mp4processor->outVector[j + k * mp4processor->RSDims] = rsOut[k];
    }
    //
    //	OK, the result is N * 110 * 8 bits 
    //	bits 0 .. 15 is firecode
    //	bit 16 is unused
    streamParameters.dacRate = (mp4processor->outVector[2] >> 6) & 01;	// bit 17
    streamParameters.sbrFlag = (mp4processor->outVector[2] >> 5) & 01;	// bit 18
    streamParameters.aacChannelMode = (mp4processor->outVector[2] >> 4) & 01;	// bit 19
    streamParameters.psFlag = (mp4processor->outVector[2] >> 3) & 01;	// bit 20
    streamParameters.mpegSurround = (mp4processor->outVector[2] & 07);	// bits 21 .. 23

//
//      added for the aac file writer
    streamParameters. CoreSrIndex   =
                  streamParameters. dacRate ?
                                (streamParameters. sbrFlag ? 6 : 3) :
                                (streamParameters. sbrFlag ? 8 : 5);
    streamParameters. CoreChConfig  =
                  streamParameters. aacChannelMode ? 2 : 1;

    streamParameters. ExtensionSrIndex =
                  streamParameters. dacRate ? 3 : 5;

    switch (2 * streamParameters.dacRate + streamParameters.sbrFlag) {
    default:		// cannot happen
    case 0:
        num_aus = 4;
        mp4processor->au_start[0] = 8;
        mp4processor->au_start[1] = mp4processor->outVector[3] * 16 + (mp4processor->outVector[4] >> 4);
        mp4processor->au_start[2] = (mp4processor->outVector[4] & 0xf) * 256 + mp4processor->outVector[5];
        mp4processor->au_start[3] = mp4processor->outVector[6] * 16 + (mp4processor->outVector[7] >> 4);
        mp4processor->au_start[4] = 110 * (mp4processor->bitRate / 8);
        break;
        //
    case 1:
        num_aus = 2;
        mp4processor->au_start[0] = 5;
        mp4processor->au_start[1] = mp4processor->outVector[3] * 16 + (mp4processor->outVector[4] >> 4);
        mp4processor->au_start[2] = 110 * (mp4processor->bitRate / 8);
        break;
        //
    case 2:
        num_aus = 6;
        mp4processor->au_start[0] = 11;
        mp4processor->au_start[1] = mp4processor->outVector[3] * 16 + (mp4processor->outVector[4] >> 4);
        mp4processor->au_start[2] = (mp4processor->outVector[4] & 0xf) * 256 + mp4processor->outVector[5];
        mp4processor->au_start[3] = mp4processor->outVector[6] * 16 + (mp4processor->outVector[7] >> 4);
        mp4processor->au_start[4] = (mp4processor->outVector[7] & 0xf) * 256 +
            mp4processor->outVector[8];
        mp4processor->au_start[5] = mp4processor->outVector[9] * 16 +
            (mp4processor->outVector[10] >> 4);
        mp4processor->au_start[6] = 110 * (mp4processor->bitRate / 8);
        break;
        //
    case 3:
        num_aus = 3;
        mp4processor->au_start[0] = 6;
        mp4processor->au_start[1] = mp4processor->outVector[3] * 16 + (mp4processor->outVector[4] >> 4);
        mp4processor->au_start[2] = (mp4processor->outVector[4] & 0xf) * 256 +
            mp4processor->outVector[5];
        mp4processor->au_start[3] = 110 * (mp4processor->bitRate / 8);
        break;
    }
    //
    //	extract the AU'si and prepare a buffer, sufficiently
    //	long for conversion to PCM samples

    for (i = 0; i < num_aus; i++) {
        int16_t	aac_frame_length;
        //
        if (mp4processor->au_start[i + 1] < mp4processor->au_start[i]) {
            tvhdebug(LS_RTLSDR, "invalid start sequence %d < %d", mp4processor->au_start [i + 1], mp4processor->au_start [i]);
            
            return 0;
        }

        aac_frame_length = mp4processor->au_start[i + 1] - mp4processor->au_start[i] - 2;
        // sanity check
        if ((aac_frame_length >= 960) || (aac_frame_length < 0)) {
            tvhdebug(LS_RTLSDR, "invalid aac_frame_length %d", aac_frame_length);
            return 0;
        }

        //	but first the crc check
        if (check_crc_bytes(&mp4processor->outVector[mp4processor->au_start[i]],
            aac_frame_length)) {
            //
            //	if there is pad handle it always
            if (((mp4processor->outVector[mp4processor->au_start[i] + 0] >> 5) & 07) == 4) {
                int16_t count = mp4processor->outVector[mp4processor->au_start[i] + 1];
                uint8_t buffer[count];
                memcpy(buffer, &mp4processor->outVector[mp4processor->au_start[i] + 2], count);
                //                 uint8_t L0   = buffer [count - 1];
                //                 uint8_t L1   = buffer [count - 2];
                //                 my_padHandler. processPAD (buffer, count - 3, L1, L0);
            }
            uint8_t fileBuffer[1024];
            memset(fileBuffer, 0, 1024);
            int size = mp4Processor_writeFrame(aac_frame_length, &streamParameters, fileBuffer, &mp4processor->outVector[mp4processor->au_start[i]]);
            mp4processor->writeCb(fileBuffer, size, &streamParameters, mp4processor->context);
#ifdef TRACE_MP4
            exit(0);
#endif
        } else {
            mp4processor->writeCb(NULL, 0, &streamParameters, mp4processor->context);
            tvherror(LS_RTLSDR, "CRC failure with dab+ frame should not happen");
        }
    }
    return 1;
}

static void AddBits (int data_new, size_t count, size_t *byte_bits, uint8_t **output) {
	while (count > 0) {
//	add new byte, if needed
	   if (*byte_bits == 0)
	      (*output)++;

	   size_t copy_bits = (count < (8 - *byte_bits)) ? count : (8 - *byte_bits);
	   uint8_t copy_data =
	       (data_new >> (count - copy_bits)) & (0xFF >> (8 - copy_bits));
	   (**output) |= copy_data << (8 - *byte_bits - copy_bits);

	   *byte_bits = (*byte_bits + copy_bits) % 8;
	   count -= copy_bits;
	}
}

static void	AddBytes (const uint8_t *data, size_t len, size_t *byte_bits, uint8_t **output) {
	for(size_t i = 0; i < len; i++)
	   AddBits (data[i], 8, byte_bits, output);
}

int16_t	mp4Processor_writeFrame(int16_t framelen,
    const stream_parms *sp,
    uint8_t *output, uint8_t *data) {
    
    size_t byte_bits = 0;
    uint8_t *pointer = output;
    
    memcpy(pointer, "\x56\xE0\x00\x20\x00", 5);
    pointer += 5-1;
    
/*        AddBits (0x2B7, 11, &byte_bits, &pointer);	// syncword
	AddBits (    0, 13, &byte_bits, &pointer);	// audioMuxLengthBytes - written later
//	AudioMuxElement(1)

	AddBits (    0, 1, &byte_bits, &pointer);	// useSameStreamMux
//	StreamMuxConfig()

	AddBits (    0, 1, &byte_bits, &pointer);	// audioMuxVersion
	AddBits (    1, 1, &byte_bits, &pointer);	// allStreamsSameTimeFraming
	AddBits (    0, 6, &byte_bits, &pointer);	// numSubFrames
	AddBits (    0, 4, &byte_bits, &pointer);	// numProgram
	AddBits (    0, 3, &byte_bits, &pointer);	// numLayer
*/
	if (sp  -> sbrFlag) {
           if (sp->psFlag) {
                  AddBits (0b11101, 5, &byte_bits, &pointer); // SBR
           } else {
                  AddBits (0b00101, 5, &byte_bits, &pointer); // SBR
           }
	   AddBits (sp -> CoreSrIndex, 4, &byte_bits, &pointer); // samplingFrequencyIndex
	   AddBits (sp -> CoreChConfig, 4, &byte_bits, &pointer); // channelConfiguration
	   AddBits (sp -> ExtensionSrIndex, 4, &byte_bits, &pointer);	// extensionSamplingFrequencyIndex
	   AddBits (0b00010, 5, &byte_bits, &pointer);		// AAC LC
	   AddBits (0b100, 3, &byte_bits, &pointer);							// GASpecificConfig() with 960 transform
	} else {
	   AddBits (0b00010, 5, &byte_bits, &pointer); // AAC LC
	   AddBits (sp -> CoreSrIndex, 4, &byte_bits, &pointer); // samplingFrequencyIndex
	   AddBits (sp -> CoreChConfig, 4, &byte_bits, &pointer); // channelConfiguration
	   AddBits (0b100, 3, &byte_bits, &pointer);							// GASpecificConfig() with 960 transform
	}

	AddBits (0b000, 3, &byte_bits, &pointer);	// frameLengthType
	AddBits (0xFF, 8, &byte_bits, &pointer);	// latmBufferFullness
	AddBits (   0, 1, &byte_bits, &pointer);	// otherDataPresent
	AddBits (   0, 1, &byte_bits, &pointer);	// crcCheckPresent

//	PayloadLengthInfo()
	for (int i = 0; i < framelen / 255; i++)
	   AddBits (0xFF, 8, &byte_bits, &pointer);
	AddBits (framelen % 255, 8, &byte_bits, &pointer);
	tvhtrace(LS_RTLSDR, "mp4 byte_bits %d", byte_bits);

	AddBytes (data, framelen, &byte_bits, &pointer);
	size_t len = (pointer-output) + 1 - 3;
	output [1] |= (len >> 8) & 0x1F;
	output [2] = len & 0xFF;
	return len + 3;
}