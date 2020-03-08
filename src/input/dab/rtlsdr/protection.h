/*
 *  Tvheadend - DAB Input System
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

#ifndef __TVH_DAB_RTLSDR_PROTECTION_H__
#define __TVH_DAB_RTLSDR_PROTECTION_H__

#include "tvheadend.h"

#include "viterbi_768/viterbi-768.h"

typedef struct protection protection_t;

struct protection
{
    int		outSize;
    struct v	vp;
    int16_t*	viterbiBlock;
    uint8_t*	indexTable;
    uint8_t*    disperseVector;
};

protection_t* protection_init(int16_t bitRate, int spiral);
protection_t* uep_protection_init(int16_t bitRate, int16_t protLevel);
protection_t* eep_protection_init(int16_t bitRate, int16_t protLevel);
protection_t* fic_protection_init (void);
void protection_deconvolve(protection_t *protection, int16_t *v, uint8_t *outBuffer);
void protection_destroy(protection_t *protection);

#endif