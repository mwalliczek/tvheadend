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

/*
 * The binary encoding maps the XML of ETSI TS 102 818 to a tree of
 * elements: tag (1 byte), length (1 byte, 0xFE: 2 more bytes, 0xFF: 3 more
 * bytes) and data. Element data consists of attributes (tags 0x80 - 0x87),
 * child elements (0x02 - 0x36) and character data (0x01). Strings may
 * contain tokens (0x01 - 0x13 without 0x09, 0x0A, 0x0D) referring to
 * the token table of the document.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "dab_epg.h"

#define MAX_TOKENS       20

typedef struct span {
  const uint8_t *d;
  size_t         len;
} span_t;

typedef struct epg_state {
  span_t                  tokens[MAX_TOKENS];
  dab_epg_contentid_t     default_id;
  char                   *default_lang;
  dab_epg_programme_cb_t  cb;
  void                   *opaque;
  int                     count;
} epg_state_t;

/* ************************************************************************
 * Low level helpers
 * ***********************************************************************/

static uint32_t get_bits(const uint8_t *d, int off, int n)
{
  uint32_t r = 0;
  int i;
  for (i = 0; i < n; i++, off++)
    r = (r << 1) | ((d[off >> 3] >> (7 - (off & 7))) & 1);
  return r;
}

static uint32_t get_uint(span_t s)
{
  uint32_t r = 0;
  size_t i;
  for (i = 0; i < s.len && i < 4; i++)
    r = (r << 8) | s.d[i];
  return r;
}

/* read the next element / attribute, returns 1 if found, 0 at the end, -1 on error */
static int next_elem(span_t parent, size_t *pos, int *tag, span_t *data)
{
  size_t i = *pos, l;

  if (i >= parent.len)
    return 0;
  if (i + 2 > parent.len)
    return -1;
  *tag = parent.d[i];
  l = parent.d[i + 1];
  i += 2;
  if (l == 0xFE) {
    if (i + 2 > parent.len)
      return -1;
    l = (parent.d[i] << 8) | parent.d[i + 1];
    i += 2;
  } else if (l == 0xFF) {
    if (i + 3 > parent.len)
      return -1;
    l = (parent.d[i] << 16) | (parent.d[i + 1] << 8) | parent.d[i + 2];
    i += 3;
  }
  if (l > parent.len - i)
    return -1;
  data->d = parent.d + i;
  data->len = l;
  *pos = i + l;
  return 1;
}

static int is_token(uint8_t c)
{
  return c >= 0x01 && c <= 0x13 && c != 0x09 && c != 0x0A && c != 0x0D;
}

/* decode a string, replacing tokens; the result has to be freed */
static char *get_string(epg_state_t *st, span_t s)
{
  size_t i, len = 0;
  char *r, *o;

  for (i = 0; i < s.len; i++)
    len += is_token(s.d[i]) ? st->tokens[s.d[i]].len : 1;
  r = o = malloc(len + 1);
  for (i = 0; i < s.len; i++) {
    if (is_token(s.d[i])) {
      span_t t = st->tokens[s.d[i]];
      if (t.len) {                    /* undefined tokens are dropped */
        memcpy(o, t.d, t.len);
        o += t.len;
      }
    } else {
      *o++ = s.d[i];
    }
  }
  *o = '\0';
  /* embedded NULs would cut the string, that is fine */
  return r;
}

/* the character data of an element */
static char *get_cdata(epg_state_t *st, span_t e)
{
  size_t pos = 0;
  span_t c;
  int tag;

  while (next_elem(e, &pos, &tag, &c) > 0)
    if (tag == DAB_EPG_TAG_CDATA)
      return get_string(st, c);
  return NULL;
}

/* an attribute of an element */
static int get_attr(span_t e, int attr, span_t *value)
{
  size_t pos = 0;
  int tag;

  while (next_elem(e, &pos, &tag, value) > 0)
    if (tag == attr)
      return 1;
  return 0;
}

static void parse_token_table(epg_state_t *st, span_t t)
{
  size_t pos = 0;
  span_t v;
  int tag;

  while (next_elem(t, &pos, &tag, &v) > 0)
    if (is_token(tag))
      st->tokens[tag] = v;
}

/* ************************************************************************
 * Data types
 * ***********************************************************************/

int dab_epg_decode_timepoint(const uint8_t *d, size_t len, time_t *t)
{
  uint32_t mjd, h, m, s = 0;
  size_t i;

  if (len < 4)
    return -1;
  for (i = 0; i < len && d[i] == 0; i++);
  if (i == len) {
    *t = 0;                           /* "now" */
    return 0;
  }
  mjd = get_bits(d, 1, 17);
  h = get_bits(d, 21, 5);
  m = get_bits(d, 26, 6);
  if (get_bits(d, 20, 1)) {           /* long form with seconds */
    if (len < 6)
      return -1;
    s = get_bits(d, 32, 6);
  }
  if (mjd < 40587 || h > 23 || m > 59 || s > 59)
    return -1;
  /* the time is UTC, the local time offset is informative only */
  *t = (time_t)(mjd - 40587) * 86400 + h * 3600 + m * 60 + s;
  return 0;
}

int dab_epg_decode_contentid(const uint8_t *d, size_t len, dab_epg_contentid_t *id)
{
  size_t i = 1;

  memset(id, 0, sizeof(*id));
  if (len < 3)
    return -1;
  id->has_ensemble = (d[0] >> 6) & 1;
  id->scids = d[0] & 0x0F;
  if (id->has_ensemble) {
    if (len < 4)
      return -1;
    id->ecc = d[1];
    id->eid = (d[2] << 8) | d[3];
    i = 4;
  }
  if ((d[0] >> 4) & 1) {              /* 32 bit data service id */
    if (i + 4 > len)
      return -1;
    id->sid = ((uint32_t)d[i] << 24) | (d[i + 1] << 16) | (d[i + 2] << 8) | d[i + 3];
  } else {
    if (i + 2 > len)
      return -1;
    id->sid = (d[i] << 8) | d[i + 1];
  }
  id->valid = 1;
  return 0;
}

static char *decode_genre(epg_state_t *st, span_t v)
{
  static const char *cs_names[] = {
    NULL, "IntentionCS", "FormatCS", "ContentCS", "IntendedAudienceCS",
    "OriginationCS", "ContentAlertCS", "MediaTypeCS", "AtmosphereCS"
  };
  char buf[128];
  size_t i, o;
  int cs;

  if (v.len == 0)
    return NULL;
  cs = v.d[0] & 0x0F;
  if (v.d[0] >= 0x10 || cs == 0 || cs > 8)
    return get_string(st, v);         /* plain href */
  o = snprintf(buf, sizeof(buf), "urn:tva:metadata:cs:%s:2002:%d", cs_names[cs], cs);
  for (i = 1; i < v.len && o < sizeof(buf) - 5; i++)
    o += snprintf(buf + o, sizeof(buf) - o, ".%d", v.d[i]);
  return strdup(buf);
}

/* ************************************************************************
 * Programmes
 * ***********************************************************************/

static void set_str(char **dst, char *src)
{
  if (*dst == NULL)
    *dst = src;
  else
    free(src);
}

static void parse_media_description(epg_state_t *st, span_t e,
                                    char **short_desc, char **long_desc)
{
  size_t pos = 0;
  span_t c;
  int tag;

  while (next_elem(e, &pos, &tag, &c) > 0) {
    if (tag == DAB_EPG_TAG_SHORT_DESCRIPTION)
      set_str(short_desc, get_cdata(st, c));
    else if (tag == DAB_EPG_TAG_LONG_DESCRIPTION)
      set_str(long_desc, get_cdata(st, c));
  }
}

static void emit_location(epg_state_t *st, dab_epg_programme_t *p, span_t loc,
                          const dab_epg_contentid_t *def)
{
  dab_epg_contentid_t bearers[16];
  int nbearers = 0, i, tag;
  size_t pos = 0;
  span_t c, v;

  while (next_elem(loc, &pos, &tag, &c) > 0)
    if (tag == DAB_EPG_TAG_BEARER && nbearers < 16 &&
        get_attr(c, 0x80, &v) &&
        dab_epg_decode_contentid(v.d, v.len, &bearers[nbearers]) == 0)
      nbearers++;
  if (nbearers == 0) {
    if (!def->valid)
      return;                         /* we do not know the service */
    bearers[nbearers++] = *def;
  }

  pos = 0;
  while (next_elem(loc, &pos, &tag, &c) > 0) {
    if (tag != DAB_EPG_TAG_TIME)
      continue;
    /* prefer the actual time over the billed time */
    if (!((get_attr(c, 0x82, &v) && dab_epg_decode_timepoint(v.d, v.len, &p->start) == 0) ||
          (get_attr(c, 0x80, &v) && dab_epg_decode_timepoint(v.d, v.len, &p->start) == 0)))
      continue;
    if (p->start == 0)
      continue;
    if (get_attr(c, 0x83, &v) || get_attr(c, 0x81, &v))
      p->duration = get_uint(v);
    else
      p->duration = 0;
    if (p->duration == 0)
      continue;
    for (i = 0; i < nbearers; i++) {
      p->service = bearers[i];
      st->count++;
      if (st->cb)
        st->cb(p, st->opaque);
    }
  }
}

static void parse_programme(epg_state_t *st, span_t e, const dab_epg_contentid_t *def)
{
  dab_epg_programme_t p;
  char *crid = NULL, *lang = NULL, *sname = NULL, *mname = NULL, *lname = NULL;
  char *sdesc = NULL, *ldesc = NULL, *genre = NULL;
  size_t pos = 0;
  span_t c, v;
  int tag;

  memset(&p, 0, sizeof(p));
  p.on_air = 1;
  while (next_elem(e, &pos, &tag, &c) > 0) {
    switch (tag) {
    case 0x80: set_str(&crid, get_string(st, c)); break;
    case 0x81: p.short_id = get_uint(c); break;
    case 0x84: p.on_air = !(c.len == 1 && c.d[0] == 0x02); break;
    case 0x86: set_str(&lang, get_string(st, c)); break;
    case DAB_EPG_TAG_SHORT_NAME:  set_str(&sname, get_cdata(st, c)); break;
    case DAB_EPG_TAG_MEDIUM_NAME: set_str(&mname, get_cdata(st, c)); break;
    case DAB_EPG_TAG_LONG_NAME:   set_str(&lname, get_cdata(st, c)); break;
    case DAB_EPG_TAG_MEDIA_DESCRIPTION:
      parse_media_description(st, c, &sdesc, &ldesc);
      break;
    case DAB_EPG_TAG_GENRE:
      if (genre == NULL && get_attr(c, 0x80, &v))
        genre = decode_genre(st, v);
      break;
    }
  }
  p.crid = crid;
  p.lang = lang ?: st->default_lang;
  p.short_name = sname;
  p.medium_name = mname;
  p.long_name = lname;
  p.short_desc = sdesc;
  p.long_desc = ldesc;
  p.genre = genre;

  pos = 0;
  while (next_elem(e, &pos, &tag, &c) > 0)
    if (tag == DAB_EPG_TAG_LOCATION)
      emit_location(st, &p, c, def);

  free(crid); free(lang); free(sname); free(mname); free(lname);
  free(sdesc); free(ldesc); free(genre);
}

static void parse_schedule(epg_state_t *st, span_t e)
{
  dab_epg_contentid_t def = st->default_id;
  size_t pos = 0, pos2;
  span_t c, c2, v;
  int tag, tag2;

  /* a schedule scope with a single service defines the service */
  while (next_elem(e, &pos, &tag, &c) > 0) {
    if (tag != DAB_EPG_TAG_SCOPE)
      continue;
    pos2 = 0;
    while (next_elem(c, &pos2, &tag2, &c2) > 0)
      if (tag2 == DAB_EPG_TAG_SERVICE_SCOPE && get_attr(c2, 0x80, &v))
        dab_epg_decode_contentid(v.d, v.len, &def);
  }

  pos = 0;
  while (next_elem(e, &pos, &tag, &c) > 0)
    if (tag == DAB_EPG_TAG_PROGRAMME)
      parse_programme(st, c, &def);
}

int dab_epg_parse(const uint8_t *data, size_t len,
                  const dab_epg_contentid_t *default_service,
                  dab_epg_programme_cb_t cb, void *opaque)
{
  epg_state_t st;
  span_t doc = { data, len }, root, c;
  size_t pos = 0;
  int tag, r;

  memset(&st, 0, sizeof(st));
  st.cb = cb;
  st.opaque = opaque;
  if (default_service)
    st.default_id = *default_service;

  if (next_elem(doc, &pos, &tag, &root) <= 0 || tag != DAB_EPG_TAG_EPG)
    return -1;

  /* document wide definitions first */
  pos = 0;
  while ((r = next_elem(root, &pos, &tag, &c)) > 0) {
    if (tag == DAB_EPG_TAG_TOKEN_TABLE)
      parse_token_table(&st, c);
    else if (tag == DAB_EPG_TAG_DEFAULT_CONTENT_ID)
      dab_epg_decode_contentid(c.d, c.len, &st.default_id);
    else if (tag == DAB_EPG_TAG_DEFAULT_LANGUAGE && st.default_lang == NULL)
      st.default_lang = get_string(&st, c);
  }
  if (r < 0) {
    free(st.default_lang);
    return -1;
  }

  pos = 0;
  while (next_elem(root, &pos, &tag, &c) > 0)
    if (tag == DAB_EPG_TAG_SCHEDULE)
      parse_schedule(&st, c);

  free(st.default_lang);
  return st.count;
}
