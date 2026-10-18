/*
 *  Tvheadend - DAB EPG / SPI binary decoder (ETSI TS 102 371)
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

#ifndef __DAB_EPG_H__
#define __DAB_EPG_H__

#include <stdint.h>
#include <stddef.h>
#include <time.h>

/* element tags (ETSI TS 102 371) */
#define DAB_EPG_TAG_CDATA               0x01
#define DAB_EPG_TAG_EPG                 0x02
#define DAB_EPG_TAG_SERVICE_INFORMATION 0x03
#define DAB_EPG_TAG_TOKEN_TABLE         0x04
#define DAB_EPG_TAG_DEFAULT_CONTENT_ID  0x05
#define DAB_EPG_TAG_DEFAULT_LANGUAGE    0x06
#define DAB_EPG_TAG_SHORT_NAME          0x10
#define DAB_EPG_TAG_MEDIUM_NAME         0x11
#define DAB_EPG_TAG_LONG_NAME           0x12
#define DAB_EPG_TAG_MEDIA_DESCRIPTION   0x13
#define DAB_EPG_TAG_GENRE               0x14
#define DAB_EPG_TAG_KEYWORDS            0x16
#define DAB_EPG_TAG_MEMBER_OF           0x17
#define DAB_EPG_TAG_LINK                0x18
#define DAB_EPG_TAG_LOCATION            0x19
#define DAB_EPG_TAG_SHORT_DESCRIPTION   0x1A
#define DAB_EPG_TAG_LONG_DESCRIPTION    0x1B
#define DAB_EPG_TAG_PROGRAMME           0x1C
#define DAB_EPG_TAG_PROGRAMME_GROUPS    0x20
#define DAB_EPG_TAG_SCHEDULE            0x21
#define DAB_EPG_TAG_ALTERNATE_SOURCE    0x22
#define DAB_EPG_TAG_PROGRAMME_GROUP     0x23
#define DAB_EPG_TAG_SCOPE               0x24
#define DAB_EPG_TAG_SERVICE_SCOPE       0x25
#define DAB_EPG_TAG_ENSEMBLE            0x26
#define DAB_EPG_TAG_FREQUENCY           0x27
#define DAB_EPG_TAG_SERVICE             0x28
#define DAB_EPG_TAG_SERVICE_ID          0x29
#define DAB_EPG_TAG_EPG_LANGUAGE        0x2A
#define DAB_EPG_TAG_MULTIMEDIA          0x2B
#define DAB_EPG_TAG_TIME                0x2C
#define DAB_EPG_TAG_BEARER              0x2D
#define DAB_EPG_TAG_PROGRAMME_EVENT     0x2E
#define DAB_EPG_TAG_RELATIVE_TIME       0x2F

/* a DAB content id (bearer) */
typedef struct dab_epg_contentid {
  int       valid;
  int       has_ensemble;
  uint8_t   ecc;
  uint16_t  eid;
  uint32_t  sid;
  uint8_t   scids;
} dab_epg_contentid_t;

/* one broadcast of a programme */
typedef struct dab_epg_programme {
  dab_epg_contentid_t service;    /* service the programme is broadcast on */
  time_t      start;              /* UTC */
  uint32_t    duration;           /* seconds */
  uint32_t    short_id;
  int         on_air;             /* 0 for broadcast="off-air" */
  const char *crid;
  const char *lang;
  const char *short_name;
  const char *medium_name;
  const char *long_name;
  const char *short_desc;
  const char *long_desc;
  const char *genre;              /* first genre, TV-Anytime href */
} dab_epg_programme_t;

typedef void (*dab_epg_programme_cb_t)(const dab_epg_programme_t *p, void *opaque);

/*
 * Parse a binary encoded programme information document. default_service
 * is used for programmes without bearer, schedule scope or default content
 * id (may be NULL). Returns the number of reported programmes, or -1 for
 * an invalid document.
 */
int dab_epg_parse(const uint8_t *data, size_t len,
                  const dab_epg_contentid_t *default_service,
                  dab_epg_programme_cb_t cb, void *opaque);

/* helpers, exported for the unit tests */
int dab_epg_decode_timepoint(const uint8_t *d, size_t len, time_t *t);
int dab_epg_decode_contentid(const uint8_t *d, size_t len, dab_epg_contentid_t *id);

#endif /* __DAB_EPG_H__ */
