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

#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "tvheadend.h"
#include "dab_mot.h"
#include "charsets.h"
#include "rtlsdr/reed-solomon.h"

#define MAX_DATAGROUP_SIZE   (8 + 2 + 2 + 15 + 8191 + 2)
#define MAX_OBJECTS          128
#define MAX_OBJECT_SIZE      (2 * 1024 * 1024)
#define MAX_SEGMENTS         (MAX_OBJECT_SIZE / 64)

/* MOT header parameters */
#define MOT_PARAM_CONTENT_NAME      0x0C
#define MOT_PARAM_COMPRESSION_TYPE  0x11
#define MOT_PARAM_EPG_SCOPE_ID      0x27

typedef struct mot_segbuf {
  uint8_t   **seg;
  uint16_t   *seglen;
  int         alloc;
  int         count;          /* number of received segments */
  int         last;           /* number of the last segment, -1 unknown */
  size_t      total;
} mot_segbuf_t;

typedef struct mot_entry {
  int           used;
  uint16_t      tid;
  uint32_t      lru;
  mot_segbuf_t  header;       /* header mode: MOT header segments */
  mot_segbuf_t  body;
  uint8_t      *hdr;          /* complete header (header mode) */
  size_t        hdr_len;
  uint8_t      *pending;      /* complete body waiting for its header */
  size_t        pending_len;
  int           delivered;
  uint32_t      delivered_crc;
} mot_entry_t;

struct dab_mot_decoder {
  uint16_t            address;
  dab_mot_object_cb_t cb;
  void               *opaque;
  dab_mot_stats_t     stats;

  /* packet stream: packets continue across logical frames, the bytes of
     an unfinished packet wait here for the next frame */
  uint8_t            *stream;
  size_t              stream_len, stream_size;
  uint32_t            frames;

  /* packet mode FEC (EN 300 401 clause 5.3.5) */
  int                 fec;
  reedSolomon_t      *rs;
  uint8_t            *fbuf;
  size_t              fbuf_len, fbuf_size;
  int                 fec_synced;
  int                 fec_layout;          /* -1: not known yet */
  int                 fec_bad_frames;

  /* packet to data group assembly */
  uint8_t            *dg;
  size_t              dg_len;
  int                 dg_active;
  int                 dg_ci;
  int                 last_ci;              /* -1: none yet */

  /* MOT directory */
  uint16_t            dir_tid;
  mot_segbuf_t        dir_segs;
  uint8_t            *dir;
  size_t              dir_len;

  mot_entry_t         entries[MAX_OBJECTS];
  uint32_t            lru_clock;
};

/* ************************************************************************
 * CRC
 * ***********************************************************************/

uint16_t dab_crc16(const uint8_t *data, size_t len)
{
  uint16_t crc = 0xFFFF;
  size_t i;
  int j;

  for (i = 0; i < len; i++) {
    crc ^= (uint16_t)data[i] << 8;
    for (j = 0; j < 8; j++)
      crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
  }
  return ~crc;
}

/* some encoders use the bit reversed X.25 variant of the same CRC */
static uint16_t dab_crc16_x25(const uint8_t *data, size_t len)
{
  uint16_t crc = 0xFFFF;
  size_t i;
  int j;

  for (i = 0; i < len; i++) {
    crc ^= data[i];
    for (j = 0; j < 8; j++)
      crc = (crc & 1) ? (crc >> 1) ^ 0x8408 : (crc >> 1);
  }
  return ~crc;
}

/* the last two bytes of data contain the CRC over the preceding bytes */
static int crc_ok(const uint8_t *data, size_t len)
{
  uint16_t crc;
  if (len < 2)
    return 0;
  crc = (data[len - 2] << 8) | data[len - 1];
  return crc == dab_crc16(data, len - 2) || crc == dab_crc16_x25(data, len - 2);
}

/* ************************************************************************
 * Segment buffers
 * ***********************************************************************/

static void segbuf_clear(mot_segbuf_t *b)
{
  int i;
  for (i = 0; i < b->alloc; i++)
    free(b->seg[i]);
  free(b->seg);
  free(b->seglen);
  memset(b, 0, sizeof(*b));
  b->last = -1;
}

/* returns 1 when all segments are present */
static int segbuf_add(mot_segbuf_t *b, int segno, int last,
                      const uint8_t *data, size_t len)
{
  if (segno >= MAX_SEGMENTS || b->total + len > MAX_OBJECT_SIZE)
    return -1;
  if (segno >= b->alloc) {
    int n = b->alloc ? b->alloc : 8;
    while (n <= segno) n *= 2;
    b->seg = realloc(b->seg, n * sizeof(uint8_t *));
    b->seglen = realloc(b->seglen, n * sizeof(uint16_t));
    memset(b->seg + b->alloc, 0, (n - b->alloc) * sizeof(uint8_t *));
    memset(b->seglen + b->alloc, 0, (n - b->alloc) * sizeof(uint16_t));
    b->alloc = n;
  }
  if (b->seg[segno] == NULL) {
    b->seg[segno] = malloc(len ? len : 1);
    memcpy(b->seg[segno], data, len);
    b->seglen[segno] = len;
    b->total += len;
    b->count++;
  }
  if (last)
    b->last = segno;
  return b->last >= 0 && b->count == b->last + 1;
}

static uint8_t *segbuf_join(mot_segbuf_t *b, size_t *len)
{
  uint8_t *r = malloc(b->total ? b->total : 1);
  size_t o = 0;
  int i;
  for (i = 0; i <= b->last; i++) {
    memcpy(r + o, b->seg[i], b->seglen[i]);
    o += b->seglen[i];
  }
  *len = o;
  return r;
}

/* ************************************************************************
 * MOT header parsing
 * ***********************************************************************/

typedef struct mot_header {
  uint32_t  body_size;
  uint32_t  header_size;
  int       content_type;
  int       content_subtype;
  char     *name;
  int       compression;
  uint8_t   scope_id[8];
  size_t    scope_id_len;
} mot_header_t;

/* parse core header + parameters, returns header size or -1 */
static int mot_header_parse(const uint8_t *h, size_t len, mot_header_t *mh)
{
  size_t i;

  memset(mh, 0, sizeof(*mh));
  if (len < 7)
    return -1;
  mh->body_size = ((uint32_t)h[0] << 20) | (h[1] << 12) | (h[2] << 4) | (h[3] >> 4);
  mh->header_size = ((h[3] & 0x0F) << 9) | (h[4] << 1) | (h[5] >> 7);
  mh->content_type = (h[5] >> 1) & 0x3F;
  mh->content_subtype = ((h[5] & 1) << 8) | h[6];
  if (mh->header_size < 7 || mh->header_size > len)
    return -1;

  for (i = 7; i < mh->header_size; ) {
    int pli = h[i] >> 6, id = h[i] & 0x3F;
    size_t dlen, hl = 1;
    switch (pli) {
    case 0: dlen = 0; break;
    case 1: dlen = 1; break;
    case 2: dlen = 4; break;
    default:
      if (i + 1 >= mh->header_size)
        return -1;
      if (h[i + 1] & 0x80) {
        if (i + 2 >= mh->header_size)
          return -1;
        dlen = ((h[i + 1] & 0x7F) << 8) | h[i + 2];
        hl = 3;
      } else {
        dlen = h[i + 1] & 0x7F;
        hl = 2;
      }
      break;
    }
    if (i + hl + dlen > mh->header_size)
      return -1;
    if (id == MOT_PARAM_CONTENT_NAME && dlen > 1 && mh->name == NULL)
      mh->name = toStringUsingCharset((const char *)&h[i + hl + 1],
                                      (CharacterSet)(h[i + hl] >> 4), dlen - 1);
    else if (id == MOT_PARAM_COMPRESSION_TYPE && dlen == 1)
      mh->compression = h[i + hl];
    else if (id == MOT_PARAM_EPG_SCOPE_ID && dlen <= sizeof(mh->scope_id)) {
      memcpy(mh->scope_id, &h[i + hl], dlen);
      mh->scope_id_len = dlen;
    }
    i += hl + dlen;
  }
  return mh->header_size;
}

/* find the header for a transport id in the current directory */
static int dir_find_header(dab_mot_decoder_t *dec, uint16_t tid,
                           const uint8_t **h, size_t *hlen)
{
  const uint8_t *d = dec->dir;
  size_t i, len = dec->dir_len, ext;

  if (d == NULL || len < 13)
    return 0;
  ext = (d[11] << 8) | d[12];
  for (i = 13 + ext; i + 2 + 7 <= len; ) {
    uint16_t etid = (d[i] << 8) | d[i + 1];
    size_t hs = ((d[i + 5] & 0x0F) << 9) | (d[i + 6] << 1) | (d[i + 7] >> 7);
    if (hs < 7 || i + 2 + hs > len)
      return 0;
    if (etid == tid) {
      *h = &d[i + 2];
      *hlen = hs;
      return 1;
    }
    i += 2 + hs;
  }
  return 0;
}

/* ************************************************************************
 * Objects
 * ***********************************************************************/

static mot_entry_t *entry_find(dab_mot_decoder_t *dec, uint16_t tid, int create)
{
  mot_entry_t *e, *oldest = NULL, *free_e = NULL;
  int i;

  for (i = 0; i < MAX_OBJECTS; i++) {
    e = &dec->entries[i];
    if (!e->used) {
      if (!free_e) free_e = e;
      continue;
    }
    if (e->tid == tid) {
      e->lru = ++dec->lru_clock;
      return e;
    }
    if (!oldest || e->lru < oldest->lru)
      oldest = e;
  }
  if (!create)
    return NULL;
  e = free_e;
  if (e == NULL) {
    /* recycle the least recently used object */
    e = oldest;
    segbuf_clear(&e->header);
    segbuf_clear(&e->body);
    free(e->hdr);
    free(e->pending);
  }
  memset(e, 0, sizeof(*e));
  e->header.last = e->body.last = -1;
  e->used = 1;
  e->tid = tid;
  e->lru = ++dec->lru_clock;
  return e;
}

static uint8_t *inflate_gzip(const uint8_t *in, size_t len, size_t *outlen)
{
  z_stream zs;
  size_t alloc = len * 4 + 1024, o = 0;
  uint8_t *out = malloc(alloc);
  int r;

  memset(&zs, 0, sizeof(zs));
  if (inflateInit2(&zs, 16 + MAX_WBITS) != Z_OK) {
    free(out);
    return NULL;
  }
  zs.next_in = (Bytef *)in;
  zs.avail_in = len;
  do {
    if (o == alloc) {
      if (alloc >= MAX_OBJECT_SIZE * 4)
        break;
      alloc *= 2;
      out = realloc(out, alloc);
    }
    zs.next_out = out + o;
    zs.avail_out = alloc - o;
    r = inflate(&zs, Z_NO_FLUSH);
    o = alloc - zs.avail_out;
  } while (r == Z_OK);
  inflateEnd(&zs);
  if (r != Z_STREAM_END) {
    free(out);
    return NULL;
  }
  *outlen = o;
  return out;
}

/* deliver the object if both header and body are available */
static void entry_try_deliver(dab_mot_decoder_t *dec, mot_entry_t *e)
{
  const uint8_t *h;
  size_t hlen;
  mot_header_t mh;
  dab_mot_object_t obj;
  uint8_t *inflated = NULL;
  uint32_t crc;

  if (e->pending == NULL)
    return;
  if (e->hdr) {
    h = e->hdr;
    hlen = e->hdr_len;
  } else if (!dir_find_header(dec, e->tid, &h, &hlen)) {
    return;
  }
  if (mot_header_parse(h, hlen, &mh) < 0) {
    tvhtrace(LS_DABEPG, "invalid MOT header for transport id %d", e->tid);
    free(mh.name);
    return;
  }

  crc = crc32(0, e->pending, e->pending_len);
  if (e->delivered && e->delivered_crc == crc) {
    /* carousel repetition of an unchanged object */
    free(mh.name);
    free(e->pending);
    e->pending = NULL;
    return;
  }

  memset(&obj, 0, sizeof(obj));
  obj.transport_id = e->tid;
  obj.content_type = mh.content_type;
  obj.content_subtype = mh.content_subtype;
  obj.name = mh.name;
  memcpy(obj.scope_id, mh.scope_id, sizeof(obj.scope_id));
  obj.scope_id_len = mh.scope_id_len;
  obj.body = e->pending;
  obj.body_len = e->pending_len;
  if (mh.compression == 1 ||
      (e->pending_len > 2 && e->pending[0] == 0x1f && e->pending[1] == 0x8b)) {
    inflated = inflate_gzip(e->pending, e->pending_len, &obj.body_len);
    if (inflated == NULL) {
      tvhtrace(LS_DABEPG, "cannot inflate MOT object %d", e->tid);
      free(mh.name);
      return;
    }
    obj.body = inflated;
  }

  tvhdebug(LS_DABEPG, "MOT object %d '%s' type %d/%d, %zu bytes", e->tid,
           mh.name ?: "", mh.content_type, mh.content_subtype, obj.body_len);
  dec->stats.objects++;
  e->delivered = 1;
  e->delivered_crc = crc;
  if (dec->cb)
    dec->cb(&obj, dec->opaque);

  free(inflated);
  free(mh.name);
  free(e->pending);
  e->pending = NULL;
}

static void handle_directory(dab_mot_decoder_t *dec, uint16_t tid, int segno,
                             int last, const uint8_t *data, size_t len)
{
  int i, r;
  size_t dlen;
  uint8_t *d;

  if (tid != dec->dir_tid) {
    /* new directory: start over */
    segbuf_clear(&dec->dir_segs);
    dec->dir_tid = tid;
  }
  r = segbuf_add(&dec->dir_segs, segno, last, data, len);
  if (r < 0) {
    segbuf_clear(&dec->dir_segs);
    return;
  }
  if (r == 0)
    return;
  d = segbuf_join(&dec->dir_segs, &dlen);
  segbuf_clear(&dec->dir_segs);
  dec->dir_tid = tid;
  if (dlen < 13 || (d[0] & 0x80)) {   /* compression flag */
    free(d);
    return;
  }
  if (dec->dir && dec->dir_len == dlen && !memcmp(dec->dir, d, dlen)) {
    free(d);
    return;
  }
  tvhdebug(LS_DABEPG, "new MOT directory with %d objects", (d[4] << 8) | d[5]);
  free(dec->dir);
  dec->dir = d;
  dec->dir_len = dlen;
  for (i = 0; i < MAX_OBJECTS; i++)
    if (dec->entries[i].used)
      entry_try_deliver(dec, &dec->entries[i]);
}

static void handle_segment(dab_mot_decoder_t *dec, int type, uint16_t tid,
                           int segno, int last, const uint8_t *data, size_t len)
{
  size_t seglen;
  mot_entry_t *e;
  mot_segbuf_t *b;
  int r;

  /* segment header: repetition count (3 bits), segment size (13 bits) */
  if (len < 2)
    return;
  seglen = ((data[0] & 0x1F) << 8) | data[1];
  if (seglen > len - 2) {
    dec->stats.datagroup_errors++;
    return;
  }
  data += 2;

  if (type == DAB_DG_MOT_DIRECTORY) {
    handle_directory(dec, tid, segno, last, data, seglen);
    return;
  }
  if (type != DAB_DG_MOT_HEADER && type != DAB_DG_MOT_BODY)
    return;

  e = entry_find(dec, tid, 1);
  b = type == DAB_DG_MOT_HEADER ? &e->header : &e->body;
  r = segbuf_add(b, segno, last, data, seglen);
  if (r < 0) {
    segbuf_clear(b);
    return;
  }
  if (r == 0)
    return;

  if (type == DAB_DG_MOT_HEADER) {
    size_t hlen;
    uint8_t *h = segbuf_join(b, &hlen);
    if (e->hdr && (e->hdr_len != hlen || memcmp(e->hdr, h, hlen)))
      e->delivered = 0;               /* changed object */
    free(e->hdr);
    e->hdr = h;
    e->hdr_len = hlen;
  } else {
    free(e->pending);
    e->pending = segbuf_join(b, &e->pending_len);
  }
  segbuf_clear(b);
  entry_try_deliver(dec, e);
}

void dab_mot_decoder_feed_datagroup(dab_mot_decoder_t *dec,
                                    const uint8_t *dg, size_t len)
{
  size_t i = 2;
  int ext, crc, seg, ua, type, last = 0, segno = 0, tidflag = 0, li;
  uint16_t tid = 0;

  dec->stats.datagroups++;
  if (len < 2)
    goto fail;
  ext  = dg[0] >> 7;
  crc  = (dg[0] >> 6) & 1;
  seg  = (dg[0] >> 5) & 1;
  ua   = (dg[0] >> 4) & 1;
  type = dg[0] & 0x0F;
  if (crc) {
    if (!crc_ok(dg, len))
      goto fail;
    len -= 2;
  }
  if (ext)
    i += 2;
  if (seg) {
    if (i + 2 > len)
      goto fail;
    last = dg[i] >> 7;
    segno = ((dg[i] & 0x7F) << 8) | dg[i + 1];
    i += 2;
  }
  if (ua) {
    if (i + 1 > len)
      goto fail;
    tidflag = (dg[i] >> 4) & 1;
    li = dg[i] & 0x0F;
    if (i + 1 + li > len || (tidflag && li < 2))
      goto fail;
    if (tidflag)
      tid = (dg[i + 1] << 8) | dg[i + 2];
    i += 1 + li;
  }
  tvhtrace(LS_DABEPG, "data group type %d, transport id %d, segment %d%s, %zu bytes",
           type, tid, segno, last ? " (last)" : "", len - i);
  /* MOT always uses segmentation and transport ids */
  if (!seg || !tidflag)
    return;
  handle_segment(dec, type, tid, segno, last, dg + i, len - i);
  return;

fail:
  tvhtrace(LS_DABEPG, "invalid data group (%zu bytes)", len);
  dec->stats.datagroup_errors++;
}

/* ************************************************************************
 * Packet mode
 * ***********************************************************************/

static void parse_packets(dab_mot_decoder_t *dec,
                          const uint8_t *data, size_t len)
{
  size_t i = 0;
  uint8_t *buf;

  if (len == 0)
    return;
  /* a packet (24 .. 96 bytes) can span logical frames, e.g. 96 byte
     packets in an 8 kbit/s sub-channel (24 bytes per frame) */
  if (dec->stream_len + len > dec->stream_size) {
    dec->stream_size = dec->stream_len + len;
    dec->stream = realloc(dec->stream, dec->stream_size);
  }
  memcpy(dec->stream + dec->stream_len, data, len);
  len += dec->stream_len;
  buf = dec->stream;

  while (i + 24 <= len) {
    const uint8_t *p = buf + i;
    size_t size = ((p[0] >> 6) + 1) * 24;
    int ci, first, last, address, useful;

    if (i + size > len)
      break;                          /* rest of the packet in the next frame */
    if (!crc_ok(p, size)) {
      dec->stats.packet_crc_errors++;
      i += 24;                        /* resynchronise, packets are 24 byte aligned */
      continue;
    }
    i += size;
    dec->stats.packets++;

    ci      = (p[0] >> 4) & 3;
    first   = (p[0] >> 3) & 1;
    last    = (p[0] >> 2) & 1;
    address = ((p[0] & 3) << 8) | p[1];
    useful  = p[2] & 0x7F;
    if (address != dec->address || address == 0 || (p[2] & 0x80))
      continue;                       /* other service, padding or command */
    if (useful > (int)size - 5)
      continue;
    /* the continuity index counts the packets of the address */
    if (dec->last_ci >= 0 && ci != ((dec->last_ci + 1) & 3))
      dec->stats.continuity_errors++;
    dec->last_ci = ci;

    if (first) {
      dec->dg_len = 0;
      dec->dg_active = 1;
    } else if (!dec->dg_active || ci != ((dec->dg_ci + 1) & 3)) {
      dec->dg_active = 0;             /* packet lost */
      continue;
    }
    dec->dg_ci = ci;
    if (dec->dg_len + useful > MAX_DATAGROUP_SIZE) {
      dec->dg_active = 0;
      continue;
    }
    memcpy(dec->dg + dec->dg_len, p + 3, useful);
    dec->dg_len += useful;
    if (last) {
      dec->dg_active = 0;
      dab_mot_decoder_feed_datagroup(dec, dec->dg, dec->dg_len);
    }
  }
  dec->stream_len = len - i;
  memmove(dec->stream, buf + i, dec->stream_len);
}

/*
 * Packet mode FEC: an FEC frame is the Application Data Table, 12 rows
 * of 188 bytes (2256 bytes of the packet stream), followed by 9 FEC
 * packets (24 bytes, address 1022, no CRC) with 22 bytes each of the
 * RS Data Table, 12 rows of 16 bytes. Every row is a RS(204,188) code
 * word (shortened RS(255,239), as in EN 300 744). Receivers without FEC
 * see the unchanged packet stream and skip the FEC packets.
 *
 * The order in which the tables are filled (row or column wise) is
 * detected from the data: with the right one the code words of a
 * correct frame have no errors. Until it is known the data passes
 * through uncorrected, like without FEC.
 */
#define FEC_ADT_SIZE      2256
#define FEC_RSD_SIZE      192
#define FEC_PACKETS       9
#define FEC_FRAME_SIZE    (FEC_ADT_SIZE + FEC_PACKETS * 24)
#define FEC_ROWS          12
#define FEC_ADT_COLS      188
#define FEC_RSD_COLS      16

static int fec_packet_count(const uint8_t *p)
{
  int i, n = 0;
  for (i = 0; i < FEC_PACKETS; i++, p += 24)
    if ((p[0] & 0xC3) == 0x03 && p[1] == 0xFE)   /* 24 bytes, address 1022 */
      n++;
  return n;
}

static inline size_t fec_adt_index(int layout, int row, int col)
{
  return (layout & 1) ? (size_t)row * FEC_ADT_COLS + col : (size_t)col * FEC_ROWS + row;
}

static inline size_t fec_rsd_index(int layout, int row, int col)
{
  return (layout & 2) ? (size_t)row * FEC_RSD_COLS + col : (size_t)col * FEC_ROWS + row;
}

/* RS decoding of all rows; corrects adt in place if fix is set.
   Returns the number of decodable rows, uncorrectable rows are counted
   in *failed, corrected bytes in *corrected */
static int fec_decode(dab_mot_decoder_t *dec, int layout, uint8_t *adt,
                      const uint8_t *rsd, int fix, int *failed, int *corrected)
{
  uint8_t cw[FEC_ADT_COLS + FEC_RSD_COLS], out[FEC_ADT_COLS];
  int row, col, r;

  *failed = *corrected = 0;
  for (row = 0; row < FEC_ROWS; row++) {
    for (col = 0; col < FEC_ADT_COLS; col++)
      cw[col] = adt[fec_adt_index(layout, row, col)];
    for (col = 0; col < FEC_RSD_COLS; col++)
      cw[FEC_ADT_COLS + col] = rsd[fec_rsd_index(layout, row, col)];
    r = reedSolomon_dec_checked(dec->rs, cw, out, 255 - (FEC_ADT_COLS + FEC_RSD_COLS));
    if (r < 0) {
      (*failed)++;
      continue;
    }
    *corrected += r;
    if (fix && r > 0)
      for (col = 0; col < FEC_ADT_COLS; col++)
        adt[fec_adt_index(layout, row, col)] = out[col];
  }
  return FEC_ROWS - *failed;
}

static void fec_frame(dab_mot_decoder_t *dec, uint8_t *frame)
{
  uint8_t rsd[FEC_PACKETS * 22];
  int i, failed, corrected, best = -1, best_ok = 0, ok;

  for (i = 0; i < FEC_PACKETS; i++)
    memcpy(rsd + i * 22, frame + FEC_ADT_SIZE + i * 24 + 2, 22);
  dec->stats.fec_frames++;

  if (dec->fec_layout < 0) {
    /* which table layout gives valid code words? With a wrong one the
       rows look random and (almost) never decode */
    for (i = 0; i < 4; i++) {
      ok = fec_decode(dec, i, frame, rsd, 0, &failed, &corrected);
      if (ok > best_ok) {
        best_ok = ok;
        best = i;
      }
    }
    if (best_ok < FEC_ROWS / 2) {
      dec->stats.fec_rows_failed += FEC_ROWS;
      return;                       /* pass through uncorrected */
    }
    dec->fec_layout = best;
    tvhdebug(LS_DABEPG, "packet address %d: FEC table layout %d (%d of %d rows decodable)",
             dec->address, best, best_ok, FEC_ROWS);
  }

  fec_decode(dec, dec->fec_layout, frame, rsd, 1, &failed, &corrected);
  dec->stats.fec_bytes_corrected += corrected;
  dec->stats.fec_rows_failed += failed;
  if (failed == FEC_ROWS) {
    /* a wrong layout guess or no signal: look again after a while */
    if (++dec->fec_bad_frames >= 20) {
      dec->fec_layout = -1;
      dec->fec_bad_frames = 0;
    }
  } else
    dec->fec_bad_frames = 0;
}

static void fec_feed(dab_mot_decoder_t *dec, const uint8_t *data, size_t len)
{
  size_t pos = 0, q, n;
  uint8_t *b;

  if (dec->fbuf_len + len > dec->fbuf_size) {
    dec->fbuf_size = dec->fbuf_len + len;
    dec->fbuf = realloc(dec->fbuf, dec->fbuf_size);
  }
  memcpy(dec->fbuf + dec->fbuf_len, data, len);
  dec->fbuf_len += len;
  b = dec->fbuf;

  while (1) {
    if (dec->fec_synced) {
      if (dec->fbuf_len - pos < FEC_FRAME_SIZE)
        break;
      if (fec_packet_count(b + pos + FEC_ADT_SIZE) < FEC_PACKETS / 2) {
        tvhtrace(LS_DABEPG, "packet address %d: FEC frame sync lost", dec->address);
        dec->fec_synced = 0;
        continue;
      }
      fec_frame(dec, b + pos);
      parse_packets(dec, b + pos, FEC_ADT_SIZE);
      pos += FEC_FRAME_SIZE;
      continue;
    }
    /* look for the FEC packets at the end of a frame (24 byte aligned),
       all of them: shifted by a packet, 8 headers would still match */
    for (q = pos; q + FEC_PACKETS * 24 <= dec->fbuf_len; q += 24)
      if (fec_packet_count(b + q) == FEC_PACKETS)
        break;
    if (q + FEC_PACKETS * 24 > dec->fbuf_len) {
      /* not found: pass the data on, keep a frame to correct it when
         its FEC packets follow */
      n = dec->fbuf_len - pos > FEC_FRAME_SIZE ? dec->fbuf_len - pos - FEC_FRAME_SIZE : 0;
      n -= n % 24;
      parse_packets(dec, b + pos, n);
      pos += n;
      break;
    }
    tvhtrace(LS_DABEPG, "packet address %d: FEC frame sync", dec->address);
    if (q - pos >= FEC_ADT_SIZE) {
      /* a complete frame in the buffer */
      parse_packets(dec, b + pos, q - FEC_ADT_SIZE - pos);
      fec_frame(dec, b + q - FEC_ADT_SIZE);
      parse_packets(dec, b + q - FEC_ADT_SIZE, FEC_ADT_SIZE);
    } else
      parse_packets(dec, b + pos, q - pos);
    pos = q + FEC_PACKETS * 24;
    dec->fec_synced = 1;
  }
  dec->fbuf_len -= pos;
  memmove(dec->fbuf, b + pos, dec->fbuf_len);
}

void dab_mot_decoder_set_fec(dab_mot_decoder_t *dec, int fec)
{
  fec = !!fec;
  if (dec->fec == fec)
    return;
  tvhdebug(LS_DABEPG, "packet address %d: packet mode FEC %s", dec->address,
           fec ? "on" : "off");
  if (dec->fbuf_len && !fec)
    parse_packets(dec, dec->fbuf, dec->fbuf_len);
  dec->fbuf_len = 0;
  dec->fec_synced = 0;
  dec->fec = fec;
  if (fec && dec->rs == NULL)
    dec->rs = init_reedSolomon(8, 0435, 0, 1, FEC_RSD_COLS);
}

void dab_mot_decoder_feed_packets(dab_mot_decoder_t *dec,
                                  const uint8_t *data, size_t len)
{
  if (dec->fec)
    fec_feed(dec, data, len);
  else
    parse_packets(dec, data, len);

  /* after about 6 s and then every 30 s (logical frames of 24 ms) */
  if (++dec->frames % 1250 == 250)
    tvhdebug(LS_DABEPG, "packet address %d: %u packets (%u CRC errors), "
             "%u data groups (%u errors), %u objects%s",
             dec->address, dec->stats.packets, dec->stats.packet_crc_errors,
             dec->stats.datagroups, dec->stats.datagroup_errors,
             dec->stats.objects, dec->fec ? "" : ", no FEC");
  if (dec->fec && dec->frames % 1250 == 250)
    tvhdebug(LS_DABEPG, "packet address %d: FEC %u frames, %u bytes corrected, "
             "%u of %u rows uncorrectable, layout %d",
             dec->address, dec->stats.fec_frames, dec->stats.fec_bytes_corrected,
             dec->stats.fec_rows_failed, dec->stats.fec_frames * FEC_ROWS,
             dec->fec_layout);
}

/* ************************************************************************
 * Life cycle
 * ***********************************************************************/

dab_mot_decoder_t *dab_mot_decoder_create
  (uint16_t packet_address, dab_mot_object_cb_t cb, void *opaque)
{
  dab_mot_decoder_t *dec = calloc(1, sizeof(*dec));
  int i;

  dec->address = packet_address;
  dec->cb = cb;
  dec->opaque = opaque;
  dec->dg = malloc(MAX_DATAGROUP_SIZE);
  dec->dir_segs.last = -1;
  dec->fec_layout = -1;
  dec->last_ci = -1;
  for (i = 0; i < MAX_OBJECTS; i++)
    dec->entries[i].header.last = dec->entries[i].body.last = -1;
  return dec;
}

void dab_mot_decoder_destroy(dab_mot_decoder_t *dec)
{
  int i;

  if (dec == NULL)
    return;
  for (i = 0; i < MAX_OBJECTS; i++) {
    mot_entry_t *e = &dec->entries[i];
    segbuf_clear(&e->header);
    segbuf_clear(&e->body);
    free(e->hdr);
    free(e->pending);
  }
  segbuf_clear(&dec->dir_segs);
  free(dec->dir);
  free(dec->dg);
  free(dec->stream);
  free(dec->fbuf);
  if (dec->rs)
    destroy_reedSolomon(dec->rs);
  free(dec);
}

const dab_mot_stats_t *dab_mot_decoder_stats(dab_mot_decoder_t *dec)
{
  return &dec->stats;
}
