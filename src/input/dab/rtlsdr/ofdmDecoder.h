/*
 *    Copyright (C) 2013 .. 2017
 *    Jan van Katwijk (J.vanKatwijk@gmail.com)
 *    Lazy Chair Computing
 *
 *    Derived from dab-cmdline, https://github.com/JvanKatwijk/dab-cmdline
 *    (library/includes/ofdm/ofdm-decoder.h)
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    OFDM decoder
 *
 *    Adapted to tvheadend by Matthias Walliczek (matthias@walliczek.de)
 */

#ifndef _OFDMDECODER_H
#define _OFDMDECODER_H

void initConstOfdmDecoder(void);
void initOfdmDecoder(struct sdr_state_t *sdr);
void destroyOfdmDecoder(struct sdr_state_t *sdr);
void processBlock_0(struct sdr_state_t *sdr, const float _Complex* v);
void decodeBlock(struct sdr_state_t *sdr, const float _Complex* v, int32_t blkno);

#endif
