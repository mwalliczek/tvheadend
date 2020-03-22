#include "tvheadend.h"
#include "service.h"
#include "input.h"

dab_service_t *
dab_service_find
  (dab_ensemble_t *mm, uint16_t sid, int create, int *save )
{
    dab_service_t* res = calloc(1, sizeof(dab_service_t));
    return res;
}

void
service_refresh_channel(service_t *t)
{
}

void dab_service_set_subchannel(dab_service_t *s, int16_t SubChId)
{
}

int
dab_ensemble_set_network_name ( dab_ensemble_t *mm, const char *name )
{
  return 0;
}
