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

#ifndef __DAB_SCAN_H__
#define __DAB_SCAN_H__

#include <stdint.h>

/* DAB Band III channels (ETSI EN 300 401 annex) */
typedef struct dab_band3_channel {
  const char *name;
  uint32_t    freq;     /* Hz */
} dab_band3_channel_t;

extern const dab_band3_channel_t dab_band3_channels[];
extern const int dab_band3_channel_count;

/* the channel name ("11D") of a frequency, NULL if it is no Band III channel */
const char *dab_band3_channel_name(uint32_t freq);

/* scan timing in seconds */
#define DAB_SCAN_NO_SIGNAL   10   /* no FIC by then: no ensemble on this frequency */
#define DAB_SCAN_STABLE       5   /* service list unchanged for that long: done */
#define DAB_SCAN_MAX         60   /* give up waiting for missing services */

typedef enum {
  DAB_SCAN_CONTINUE,
  DAB_SCAN_COMPLETE,    /* all services found */
  DAB_SCAN_PARTIAL,     /* some services without sub-channel at the end */
  DAB_SCAN_NO_DATA,     /* nothing received */
} dab_scan_result_t;

typedef struct dab_scan_state {
  int ticks;            /* seconds since the scan started */
  int stable;           /* seconds without a change */
  int last_complete;
  int last_incomplete;
} dab_scan_state_t;

/*
 * Called once per second while an ensemble is scanned. synced: the FIC was
 * decoded (ensemble label received); complete: services with name and
 * sub-channel; incomplete: services with a name but no sub-channel yet.
 */
dab_scan_result_t dab_scan_tick(dab_scan_state_t *st, int synced,
                                int complete, int incomplete);

#endif /* __DAB_SCAN_H__ */
