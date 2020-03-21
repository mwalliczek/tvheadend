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

void sdr_dab_service_instance_dataCallback(uint8_t* result, int16_t resultLength, stream_parms* stream_parms, void* context);

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

    res->mp4processor = init_mp4processor(res->subChannel->BitRate, res, sdr_dab_service_instance_dataCallback);

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
}

void
sdr_dab_service_instance_process_data(sdr_dab_service_instance_t *sds, const int16_t *v) {
    memcpy(sds->theData[sds->nextIn], v, sds->fragmentSize * sizeof(int16_t));
    processSegment(sds, sds->theData[sds->nextIn]);
    sds->nextIn = (sds->nextIn + 1) % 20;
}

static const int aac_sample_rates[4] =
{
  32000, 16000, 48000, 24000
};

void sdr_dab_service_instance_dataCallback(uint8_t* result, int16_t resultLength, stream_parms* sp, void* context) {
  sdr_dab_service_instance_t *sds = (sdr_dab_service_instance_t *) context;
  dab_service_t *t = sds->dai_service;
  int sr = aac_sample_rates[sp->dacRate << 1 | sp->sbrFlag];
  int duration = 90000 * 1024 / sr;
  int sri = rate_to_sri(sr);
  int channels;
  switch (sp->mpegSurround) {
  default:
      tvhtrace(LS_RTLSDR, "Unrecognized mpeg_surround_config ignored");
      //      not nice, but deliberate: fall through
  case 0:
      if (sp->sbrFlag && !sp->aacChannelMode && sp->psFlag)
          channels = 2; /* Parametric stereo */
      else
          channels = 1 << sp->aacChannelMode;
      break;

  case 1:
      channels = 6;
      break;
  }

  if (resultLength > 0) {
    tvhtrace(LS_RTLSDR, "mp4 callback len %d (duration %d, channels %d)", resultLength, duration, channels);

    tvh_mutex_lock(&t->s_stream_mutex);
    service_set_streaming_status_flags((service_t *)t, TSS_PACKETS);
    t->s_streaming_live |= TSS_LIVE;
    if (streaming_pad_probe_type(&t->s_streaming_pad, SMT_PACKET)) {
      th_pkt_t *pkt = pkt_alloc(SCT_MP4A, result, resultLength, sds->dts, sds->dts, sds->dts);
      pkt->pkt_duration = duration;
      pkt->a.pkt_keyframe = 1;
      pkt->a.pkt_channels = channels;
      pkt->a.pkt_sri = sri;
  //    pkt->pkt_err = st->es_buf_a.sb_err;
      pkt->pkt_componentindex = 1;

      /* Forward packet */
      streaming_service_deliver((service_t *)t, streaming_msg_create_pkt(pkt));
      

      /* Decrease our own reference to the packet */
      pkt_ref_dec(pkt);
    }
    tvh_mutex_unlock(&t->s_stream_mutex);
  }
  sds->dts += duration;
}
