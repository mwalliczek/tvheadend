/*
 *  Tvheadend - DAB packet mode / MSC data group / MOT decoder
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

/*
 * Reassembles MOT objects (ETSI EN 301 234) transported in MSC data
 * groups (ETSI EN 300 401 clause 5.3.3) in a packet mode sub-channel
 * (ETSI EN 300 401 clause 5.3.2). Header mode and (uncompressed)
 * directory mode carousels are supported, gzip compressed bodies are
 * inflated.
 */

#ifndef __DAB_MOT_H__
#define __DAB_MOT_H__

#include <stdint.h>
#include <stddef.h>

/* MOT content types used by the DAB EPG (ETSI TS 102 371) */
#define DAB_MOT_CT_EPG                  7
#define DAB_MOT_CST_EPG_SI              0
#define DAB_MOT_CST_EPG_PI              1
#define DAB_MOT_CST_EPG_GI              2

/* MSC data group types */
#define DAB_DG_MOT_HEADER               3
#define DAB_DG_MOT_BODY                 4
#define DAB_DG_MOT_DIRECTORY            6
#define DAB_DG_MOT_DIRECTORY_COMPRESSED 7

typedef struct dab_mot_object {
  uint16_t        transport_id;
  int             content_type;
  int             content_subtype;
  char           *name;           /* ContentName as UTF-8, may be NULL */
  uint8_t         scope_id[8];    /* EPG ScopeId parameter (a content id) */
  size_t          scope_id_len;
  const uint8_t  *body;           /* uncompressed body */
  size_t          body_len;
} dab_mot_object_t;

typedef void (*dab_mot_object_cb_t)(const dab_mot_object_t *obj, void *opaque);

typedef struct dab_mot_decoder dab_mot_decoder_t;

/* statistics, mainly for tests and traces */
typedef struct dab_mot_stats {
  uint32_t packets;
  uint32_t packet_crc_errors;
  uint32_t datagroups;
  uint32_t datagroup_errors;
  uint32_t objects;
} dab_mot_stats_t;

dab_mot_decoder_t *dab_mot_decoder_create
  (uint16_t packet_address, dab_mot_object_cb_t cb, void *opaque);
void dab_mot_decoder_destroy(dab_mot_decoder_t *dec);

/* one logical frame (24 ms) of a packet mode sub-channel, as bytes */
void dab_mot_decoder_feed_packets
  (dab_mot_decoder_t *dec, const uint8_t *data, size_t len);

/* one complete MSC data group */
void dab_mot_decoder_feed_datagroup
  (dab_mot_decoder_t *dec, const uint8_t *dg, size_t len);

const dab_mot_stats_t *dab_mot_decoder_stats(dab_mot_decoder_t *dec);

/* CRC as used by DAB packets, data groups and FIBs (CRC-16-CCITT, inverted) */
uint16_t dab_crc16(const uint8_t *data, size_t len);

#endif /* __DAB_MOT_H__ */
