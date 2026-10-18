/*
 *    Copyright (C) 2013 .. 2017
 *    Jan van Katwijk (J.vanKatwijk@gmail.com)
 *    Lazy Chair Computing
 *
 *    Derived from dab-cmdline, https://github.com/JvanKatwijk/dab-cmdline
 *    (library/includes/ofdm/phasereference.h)
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
 *    Phase reference: frame start and coarse frequency offset
 *
 *    Adapted to tvheadend by Matthias Walliczek (matthias@walliczek.de)
 */

#ifndef _PHASEREFERENCE_H
#define _PHASEREFERENCE_H

#include "tvheadend.h"
#include "../dab_constants.h"
#include "dab.h"

void initConstPhaseReference(void);
void initPhaseReference(struct sdr_state_t *sdr);
void destroyPhaseReference(struct sdr_state_t *sdr);

int32_t	phaseReferenceFindIndex(struct sdr_state_t *sdr, const float _Complex* v);
int16_t phaseReferenceEstimateOffset(struct sdr_state_t *sdr, const float _Complex* v);

#endif
