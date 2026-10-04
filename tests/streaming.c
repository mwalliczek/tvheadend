#include "tvheadend.h"
#include "streaming.h"

/**
 *
 */
streaming_message_t *
streaming_msg_create(streaming_message_type_t type)
{
  streaming_message_t *sm = malloc(sizeof(streaming_message_t));
  sm->sm_type = type;
#if ENABLE_TIMESHIFT
  sm->sm_time = 0;
#endif
  return sm;
}


/**
 *
 */
streaming_message_t *
streaming_msg_create_pkt(th_pkt_t *pkt)
{
  streaming_message_t *sm = streaming_msg_create(SMT_PACKET);
  sm->sm_data = pkt;
  pkt_ref_inc(pkt);
  return sm;
}


streaming_message_t* streaming_msg_clone(streaming_message_t *src) {
    return NULL;
}

void
streaming_service_deliver(service_t *t, streaming_message_t *sm)
{
}

const char *
streaming_component_type2txt(streaming_component_type_t s)
{
    return NULL;
}
