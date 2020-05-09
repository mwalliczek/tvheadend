#include "tvheadend.h"
#include "service.h"
#include "input.h"

dab_service_t *
dab_service_find
  (dab_ensemble_t *mm, uint32_t sid, int create, int *save )
{
    dab_service_t* res = calloc(1, sizeof(dab_service_t));
    return res;
}

void
service_refresh_channel(service_t *t)
{
}

int dab_service_add_stream(dab_service_t *s, int16_t SubChId, streaming_component_type_t hts_stream_type)
{
  elementary_set_t *set = &s->s_components;
  elementary_stream_t *st = calloc(1, sizeof(elementary_stream_t));
  st->es_index = 1;
  st->es_pid = SubChId;
  st->es_type = hts_stream_type;
  st->es_cc = -1;

  TAILQ_INSERT_TAIL(&set->set_all, st, es_link);
  return 1;
}

int
dab_ensemble_set_network_name ( dab_ensemble_t *mm, const char *name )
{
  return 0;
}
