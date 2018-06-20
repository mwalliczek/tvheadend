#ifndef _FICHANDLER_H
#define _FICHANDLER_H

#include "dab.h"

void initFicHandler(struct sdr_state_t *sdr);
void process_ficBlock(struct sdr_state_t *sdr, struct complex_t* v, int16_t blkno);

#endif