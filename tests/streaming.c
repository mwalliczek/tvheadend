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

/* the last message delivered by a service, for the tests to inspect */
streaming_message_t *streaming_last_delivered;

void
streaming_service_deliver(service_t *t, streaming_message_t *sm)
{
  if (streaming_last_delivered) {
    if (streaming_last_delivered->sm_type == SMT_PACKET)
      pkt_ref_dec(streaming_last_delivered->sm_data);
    free(streaming_last_delivered);
  }
  streaming_last_delivered = sm;
}

const char *
streaming_component_type2txt(streaming_component_type_t s)
{
    return NULL;
}
