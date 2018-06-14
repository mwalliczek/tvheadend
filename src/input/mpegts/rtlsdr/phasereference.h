#ifndef _PHASEREFERENCE_H
#define _PHASEREFERENCE_H

#include "tvheadend.h"
#include "dab_constants.h"

void initPhaseReference(void);
void destoryPhaseReference(void);

int32_t	phaseReferenceFindIndex(struct complex_t* v);

#endif