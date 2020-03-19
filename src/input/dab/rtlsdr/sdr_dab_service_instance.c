/*
 *  Tvheadend - RTL SDR Input System
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

#include "dab.h"
#include "tvheadend.h"

void sdr_dab_service_instance_dataCallback(void* context, uint8_t* result, int16_t resultLength);

sdr_dab_service_instance_t * 
sdr_dab_service_instance_create(dab_service_t* service)
{
    int i;
    sdr_dab_service_instance_t* res = calloc(1, sizeof(sdr_dab_service_instance_t));
    memset(res, 0, sizeof(sdr_dab_service_instance_t));
    res->dai_service = service;
    res->subChannel = &service->s_dab_ensemble->subChannels[service->subChId];

    res->outV = calloc(24 * res->subChannel->BitRate, sizeof(uint8_t));

    res->fragmentSize = res->subChannel->Length * CUSize;

    for (i = 0; i < 16; i++) {
        res->interleaveData[i] = calloc(res->fragmentSize, sizeof(int16_t));
        memset(res->interleaveData[i], 0, res->fragmentSize * sizeof(int16_t));
    }

    res->interleaverIndex = 0;
    res->countforInterleaver = 0;

    if (res->subChannel->shortForm)
        res->protection = uep_protection_init(res->subChannel->BitRate,
            res->subChannel->protLevel);
    else
        res->protection = eep_protection_init(res->subChannel->BitRate,
            res->subChannel->protLevel);

    res->mp4processor = init_mp4processor(res->subChannel->BitRate, service, sdr_dab_service_instance_dataCallback);

    res->tempX = calloc(res->fragmentSize, sizeof(int16_t));
    res->nextIn = 0;
    res->nextOut = 0;
    for (i = 0; i < 20; i++)
        res->theData[i] = calloc(res->fragmentSize, sizeof(int16_t));

    tvhdebug(LS_RTLSDR, "created sdr_dab_service_instance_t %p", res);

    return res;
}

void sdr_dab_service_instance_destroy(sdr_dab_service_instance_t* sds) {
    int i;

    tvhdebug(LS_RTLSDR, "destroy sdr_dab_service_instance_t %p", sds);

    free(sds->tempX);
    free(sds->outV);
    for (i = 0; i < 16; i++)
        free(sds->interleaveData[i]);
    for (i = 0; i < 20; i++)
        free(sds->theData[i]);
    protection_destroy(sds->protection);
    destroy_mp4processor(sds->mp4processor);
    free(sds);
}

const	int16_t interleaveMap[] = { 0,8,4,12,2,10,6,14,1,9,5,13,3,11,7,15 };

void    processSegment(sdr_dab_service_instance_t *sds, const int16_t *Data);

static void dab_flush(dab_service_t *t, sbuf_t *sb);

#define DAB_BUFSIZE (6 * 1024)

void    processSegment(sdr_dab_service_instance_t *sds, const int16_t *Data) {
    int16_t i;

    for (i = 0; i < sds->fragmentSize; i++) {
        sds->tempX[i] = sds->interleaveData[(sds->interleaverIndex +
            interleaveMap[i & 017]) & 017][i];
        sds->interleaveData[sds->interleaverIndex][i] = Data[i];
    }

    sds->interleaverIndex = (sds->interleaverIndex + 1) & 0x0F;

    //  only continue when de-interleaver is filled
    if (sds->countforInterleaver <= 15) {
        sds->countforInterleaver++;
        return;
    }

    protection_deconvolve(sds->protection, sds->tempX, sds->outV);

    mp4Processor_addtoFrame(sds->mp4processor, sds->outV);
    
    if(sds->dai_service->s_tsbuf.sb_ptr > 0) {
      dab_service_t* t = (dab_service_t*)sds->dai_service;
      tvh_mutex_lock(&t->s_stream_mutex);
      service_set_streaming_status_flags((service_t *)t, TSS_PACKETS);
      t->s_streaming_live |= TSS_LIVE;
      if (streaming_pad_probe_type(&t->s_streaming_pad, SMT_DAB)) {
        dab_flush(t, &t->s_tsbuf);
      } else {
        sbuf_reset(&t->s_tsbuf, 2*DAB_BUFSIZE);
      }
      tvh_mutex_unlock(&t->s_stream_mutex);
    }
}

void
sdr_dab_service_instance_process_data(sdr_dab_service_instance_t *sds, const int16_t *v) {
    memcpy(sds->theData[sds->nextIn], v, sds->fragmentSize * sizeof(int16_t));
    processSegment(sds, sds->theData[sds->nextIn]);
    sds->nextIn = (sds->nextIn + 1) % 20;
}

void sdr_dab_service_instance_dataCallback(void* context, uint8_t* result, int16_t resultLength) {
  dab_service_t *t = (dab_service_t *) context;
  sbuf_t *sb = &t->s_tsbuf;

  tvhtrace(LS_RTLSDR, "mp4 callback %d", resultLength);
  if (sb->sb_data == NULL)
    sbuf_init_fixed(sb, DAB_BUFSIZE);

  sbuf_append(sb, result, resultLength);
//  sb->sb_err += errors;
}

/**
 *
 */
static void
dab_flush(dab_service_t *t, sbuf_t *sb)
{
  streaming_message_t sm;
  pktbuf_t *pb;

  t->s_tsbuf_last = mclk();

  tvhtrace(LS_RTLSDR, "mp4 flush %d", sb->sb_ptr);
  pb = pktbuf_alloc(sb->sb_data, sb->sb_ptr);
  pb->pb_err = sb->sb_err;

  memset(&sm, 0, sizeof(sm));
  sm.sm_type = SMT_DAB;
  sm.sm_data = pb;
  streaming_service_deliver((service_t *)t, streaming_msg_clone(&sm));

  pktbuf_ref_dec(pb);

  sbuf_reset(sb, 2*DAB_BUFSIZE);
}
