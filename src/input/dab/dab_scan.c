/*
 *  Tvheadend - DAB ensemble scan helpers
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

#include <stddef.h>

#include "dab_scan.h"

const dab_band3_channel_t dab_band3_channels[] = {
  { "5A",  174928000 }, { "5B",  176640000 }, { "5C",  178352000 }, { "5D",  180064000 },
  { "6A",  181936000 }, { "6B",  183648000 }, { "6C",  185360000 }, { "6D",  187072000 },
  { "7A",  188928000 }, { "7B",  190640000 }, { "7C",  192352000 }, { "7D",  194064000 },
  { "8A",  195936000 }, { "8B",  197648000 }, { "8C",  199360000 }, { "8D",  201072000 },
  { "9A",  202928000 }, { "9B",  204640000 }, { "9C",  206352000 }, { "9D",  208064000 },
  { "10A", 209936000 }, { "10B", 211648000 }, { "10C", 213360000 }, { "10D", 215072000 },
  { "11A", 216928000 }, { "11B", 218640000 }, { "11C", 220352000 }, { "11D", 222064000 },
  { "12A", 223936000 }, { "12B", 225648000 }, { "12C", 227360000 }, { "12D", 229072000 },
  { "13A", 230784000 }, { "13B", 232496000 }, { "13C", 234208000 }, { "13D", 235776000 },
  { "13E", 237488000 }, { "13F", 239200000 },
};

const int dab_band3_channel_count =
  sizeof(dab_band3_channels) / sizeof(dab_band3_channels[0]);

const char *dab_band3_channel_name(uint32_t freq)
{
  int i;
  for (i = 0; i < dab_band3_channel_count; i++)
    if (dab_band3_channels[i].freq == freq)
      return dab_band3_channels[i].name;
  return NULL;
}

dab_scan_result_t dab_scan_tick(dab_scan_state_t *st, int synced,
                                int complete, int incomplete)
{
  st->ticks++;

  if (complete == st->last_complete && incomplete == st->last_incomplete)
    st->stable++;
  else
    st->stable = 0;
  st->last_complete = complete;
  st->last_incomplete = incomplete;

  /* nothing on this frequency */
  if (!synced && complete + incomplete == 0) {
    if (st->ticks >= DAB_SCAN_NO_SIGNAL)
      return DAB_SCAN_NO_DATA;
    return DAB_SCAN_CONTINUE;
  }

  /* the FIC repeats everything within a few seconds */
  if (synced && complete > 0 && incomplete == 0 && st->stable >= DAB_SCAN_STABLE)
    return DAB_SCAN_COMPLETE;

  if (st->ticks >= DAB_SCAN_MAX) {
    if (complete > 0 && incomplete == 0)
      return DAB_SCAN_COMPLETE;
    if (complete > 0)
      return DAB_SCAN_PARTIAL;
    return DAB_SCAN_NO_DATA;
  }
  return DAB_SCAN_CONTINUE;
}
