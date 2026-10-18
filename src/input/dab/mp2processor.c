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

#include <stdlib.h>
#include <string.h>

#include "mp2processor.h"

/* Layer II bit rates (kbit/s): MPEG-1 and MPEG-2 LSF */
static const int mp2_bitrates[2][16] = {
  { 0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, 0 },
  { 0,  8, 16, 24, 32, 40, 48,  56,  64,  80,  96, 112, 128, 144, 160, 0 },
};

static const int mp2_samplerates[4] = { 44100, 48000, 32000, 0 };

/* the largest Layer II frame: 384 kbit/s at 32 kHz */
#define MP2_MAX_FRAME (144 * 384000 / 32000 + 1)

int mp2_frame_length(const uint8_t *h, int *sampleRate, int *channels)
{
  int lsf, br, sr;

  if (h[0] != 0xff || (h[1] & 0xf0) != 0xf0)
    return 0;                   /* sync word, no MPEG-2.5 */
  if (((h[1] >> 1) & 3) != 2)
    return 0;                   /* Layer II only */
  lsf = ((h[1] >> 3) & 1) ^ 1;
  br = mp2_bitrates[lsf][h[2] >> 4];
  sr = mp2_samplerates[(h[2] >> 2) & 3];
  if (br == 0 || sr == 0 || (h[3] & 3) == 2)
    return 0;                   /* free format, reserved values */
  sr >>= lsf;
  if (sampleRate)
    *sampleRate = sr;
  if (channels)
    *channels = ((h[3] >> 6) & 3) == 3 ? 1 : 2;
  return 144 * br * 1000 / sr + ((h[2] >> 1) & 1);
}

mp2processor_t *init_mp2processor(int16_t bitRate, void *context,
                                  mp2processor_cb_t writeCb)
{
  mp2processor_t *mp2 = calloc(1, sizeof(*mp2));

  mp2->bitRate = bitRate;
  /* the input of one logical frame plus a frame and a header waiting
     for the next one */
  mp2->size = 3 * bitRate + MP2_MAX_FRAME + 4;
  mp2->buf = malloc(mp2->size);
  mp2->context = context;
  mp2->writeCb = writeCb;
  return mp2;
}

void destroy_mp2processor(mp2processor_t *mp2)
{
  if (mp2 == NULL)
    return;
  free(mp2->buf);
  free(mp2);
}

/* hand out the complete frames in the buffer */
static void mp2_frames(mp2processor_t *mp2)
{
  int pos = 0, flen, sr, ch, next;

  while (mp2->len - pos >= 4) {
    flen = mp2_frame_length(mp2->buf + pos, &sr, &ch);
    if (flen == 0) {
      if (mp2->synced) {
        mp2->synced = 0;
        mp2->statLostSync++;
      }
      pos++;
      continue;
    }
    if (!mp2->synced) {
      /* a header found by search: only trust it when the next frame
         starts right behind it */
      if (mp2->len - pos < flen + 4)
        break;
      if (mp2_frame_length(mp2->buf + pos + flen, NULL, NULL) == 0) {
        pos++;
        continue;
      }
    } else if (mp2->len - pos < flen) {
      break;
    }
    next = pos + flen;
    mp2->synced = 1;
    mp2->statFrames++;
    mp2->writeCb(mp2->buf + pos, flen, sr, ch, mp2->context);
    pos = next;
  }

  if (pos > 0) {
    mp2->len -= pos;
    memmove(mp2->buf, mp2->buf + pos, mp2->len);
  }
}

void mp2Processor_addtoFrame(mp2processor_t *mp2, const uint8_t *V)
{
  int i, n = 3 * mp2->bitRate;
  uint8_t *d;

  if (mp2->len + n > mp2->size) {
    /* no sync for a long time: keep the last bytes only */
    i = mp2->len - (mp2->size - n);
    mp2->len -= i;
    memmove(mp2->buf, mp2->buf + i, mp2->len);
  }
  d = mp2->buf + mp2->len;
  for (i = 0; i < n; i++, V += 8)
    d[i] = (V[0] << 7) | (V[1] << 6) | (V[2] << 5) | (V[3] << 4) |
           (V[4] << 3) | (V[5] << 2) | (V[6] << 1) | V[7];
  mp2->len += n;
  mp2_frames(mp2);
}
