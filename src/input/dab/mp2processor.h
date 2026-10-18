/*
 *  Tvheadend - DAB (MPEG-1/2 Audio Layer II) frame processor
 *
 *  Copyright (C) 2026 Matthias Walliczek
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

#ifndef _MP2PROCESSOR_H
#define _MP2PROCESSOR_H

#include <stdint.h>

/*
 * Classic DAB audio (ASCTy != 63) is MPEG-1 (48 kHz) or MPEG-2 LSF
 * (24 kHz) Audio Layer II (EN 300 401 clause 7). The sub-channel carries
 * the frames back to back; this processor finds the frame boundaries and
 * hands out whole frames, it does not decode the audio.
 */

typedef struct mp2processor mp2processor_t;

typedef void (*mp2processor_cb_t)(const uint8_t *frame, int len,
                                  int sampleRate, int channels,
                                  void *context);

struct mp2processor {
    int16_t	bitRate;
    uint8_t	*buf;           /* bytes not yet handed out */
    int		len;
    int		size;
    int		synced;         /* the last frame was followed by a header */

    /* statistics, summed up until read (sdr_dab_service_instance_stats) */
    uint32_t	statFrames;     /* audio frames */
    uint32_t	statLostSync;   /* frame sync lost, bytes skipped */

    void	*context;
    mp2processor_cb_t writeCb;
};

mp2processor_t *init_mp2processor(int16_t bitRate, void *context,
                                  mp2processor_cb_t writeCb);
void destroy_mp2processor(mp2processor_t *mp2);
/* V: the 24 * bitRate bits of a logical frame (24 ms), one bit per byte */
void mp2Processor_addtoFrame(mp2processor_t *mp2, const uint8_t *V);

/* header checks, exported for the tests: frame length in bytes, or 0 */
int mp2_frame_length(const uint8_t *h, int *sampleRate, int *channels);

#endif
