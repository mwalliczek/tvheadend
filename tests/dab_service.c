#include "tvheadend.h"
#include "service.h"
#include "input.h"

/* last ensemble name set through dab_ensemble_set_network_name */
char *test_network_name;

dab_service_t *
dab_service_find
  (dab_ensemble_t *mm, uint16_t sid, int create, int *save )
{
    dab_service_t* res;
    TAILQ_FOREACH(res, &service_all, s_all_link) {
        if (service_id16(res) == sid)
            return res;
    }
    if (!create)
        return NULL;
    res = calloc(1, sizeof(dab_service_t));
    res->s_components.set_service_id = sid;
    res->s_dab_ensemble = mm;
    res->subChId = -1;
    TAILQ_INSERT_TAIL(&service_all, res, s_all_link);
    return res;
}

void
service_refresh_channel(service_t *t)
{
}

void dab_service_set_subchannel(dab_service_t *s, int16_t SubChId)
{
    s->subChId = SubChId;
}

int
dab_ensemble_set_network_name ( dab_ensemble_t *mm, const char *name )
{
  free(test_network_name);
  test_network_name = name ? strdup(name) : NULL;
  return 0;
}
