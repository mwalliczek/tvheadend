#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tvheadend.h"
#include "dab.h"
#include "viterbi.h"
#include "fic.h"
#include "misc.h"

void init_dab_state(struct dab_state_t **dab, void (* eti_callback)(uint8_t *eti))
{
  int i;

  *dab = calloc(sizeof(struct dab_state_t),1);

  (*dab)->eti_callback = eti_callback;

  for (i=0;i<64;i++) { (*dab)->ens_info.subchans[i].id = -1; (*dab)->ens_info.subchans[i].ASCTy = -1; }
  (*dab)->ens_info.CIFCount_hi = 0xff;
  (*dab)->ens_info.CIFCount_lo = 0xff;

#ifdef ENABLE_SPIRAL_VITERBI
  /* TODO: What is the maximum size of a sub-channel? */
  (*dab)->v = create_viterbi(768*18);
#else
  init_viterbi();
#endif
}
