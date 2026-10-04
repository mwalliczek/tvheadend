/*
 *  Electronic Program Guide - DAB EPG (SPI, ETSI TS 102 371) grabber
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
 * The programme information is decoded by the DAB input while an
 * ensemble is tuned (see rtlsdr_frontend_epg_check) and queued here as
 * MOT objects; this module maps the programmes to the channels of the
 * DAB services.
 */

#include <string.h>

#include "tvheadend.h"
#include "channels.h"
#include "service.h"
#include "epg.h"
#include "epggrab.h"
#include "epggrab/private.h"
#include "input.h"
#include "input/dab/dab_epg.h"
#include "input/dab/dab_mot.h"

typedef struct dab_epggrab_data {
  char      ensemble[UUID_HEX_SIZE];
  uint8_t   scope_id[8];
  uint32_t  scope_id_len;
} dab_epggrab_data_t;

typedef struct dab_epggrab_ctx {
  epggrab_module_t *mod;
  dab_ensemble_t   *mm;
  int               save;
  int               broadcasts;
} dab_epggrab_ctx_t;

static epggrab_module_t *dab_epggrab_mod;

int dab_epggrab_enabled(void)
{
  return dab_epggrab_mod && dab_epggrab_mod->enabled;
}

/*
 * Called by the DAB input (demodulator thread) for every new MOT object
 * of the EPG component
 */
void dab_epggrab_queue(dab_ensemble_t *mm, const dab_mot_object_t *obj)
{
  dab_epggrab_data_t hdr;

  if (!dab_epggrab_enabled())
    return;
  /* only programme information is used, service and group information
     is already known from the FIC */
  if (obj->content_type != DAB_MOT_CT_EPG ||
      obj->content_subtype != DAB_MOT_CST_EPG_PI ||
      obj->body_len < 2 || obj->body[0] != DAB_EPG_TAG_EPG)
    return;
  memset(&hdr, 0, sizeof(hdr));
  idnode_uuid_as_str(&mm->mm_id, hdr.ensemble);
  memcpy(hdr.scope_id, obj->scope_id, sizeof(hdr.scope_id));
  hdr.scope_id_len = obj->scope_id_len;
  tvhdebug(LS_DABEPG, "%s: queue programme information '%s' (%zu bytes)",
           mm->mm_nicename, obj->name ?: "", obj->body_len);
  epggrab_queue_data(dab_epggrab_mod, &hdr, sizeof(hdr), obj->body, obj->body_len);
}

/* TV-Anytime ContentCS (urn:tva:metadata:cs:ContentCS:2002:3.x.y) to EN 300 468 */
static uint8_t dab_epggrab_genre(const char *href)
{
  static const struct { const char *tva; uint8_t dvb; } map[] = {
    { "3.1.1",  0x20 },   /* news */
    { "3.1.3",  0x70 },   /* arts */
    { "3.1.5",  0x90 },   /* sciences */
    { "3.1",    0x80 },   /* information / social, political */
    { "3.2",    0x40 },   /* sports */
    { "3.6.1",  0x64 },   /* classical music */
    { "3.6.2",  0x63 },   /* jazz */
    { "3.6.4",  0x61 },   /* rock / pop */
    { "3.6",    0x60 },   /* music */
  };
  const char *p;
  size_t i, l;

  if (href == NULL || (p = strstr(href, "ContentCS:")) == NULL)
    return 0;
  p = strrchr(p, ':');
  if (p == NULL)
    return 0;
  p++;
  for (i = 0; i < ARRAY_SIZE(map); i++) {
    l = strlen(map[i].tva);
    if (!strncmp(p, map[i].tva, l) && (p[l] == '\0' || p[l] == '.'))
      return map[i].dvb;
  }
  return 0;
}

static dab_ensemble_t *
dab_epggrab_find_ensemble(dab_ensemble_t *mm, const dab_epg_contentid_t *id)
{
  dab_ensemble_t *mm2;

  if (!id->has_ensemble || (mm->mm_onid & 0xFFFF) == id->eid)
    return mm;
  /* the programme is broadcast in another ensemble of the network */
  LIST_FOREACH(mm2, &mm->mm_network->mn_ensembles, mm_network_link)
    if ((mm2->mm_onid & 0xFFFF) == id->eid)
      return mm2;
  return NULL;
}

static void dab_epggrab_programme(const dab_epg_programme_t *p, void *opaque)
{
  dab_epggrab_ctx_t *ctx = opaque;
  dab_ensemble_t *mm;
  dab_service_t *s;
  idnode_list_mapping_t *ilm;
  channel_t *ch;
  epg_broadcast_t *ebc;
  epg_changes_t changes;
  lang_str_t *title = NULL, *summary = NULL, *desc = NULL;
  epg_genre_list_t *genre = NULL;
  const char *name;
  uint8_t g;
  int save;

  if (!p->on_air || p->service.sid > 0xFFFF)
    return;
  if ((mm = dab_epggrab_find_ensemble(ctx->mm, &p->service)) == NULL)
    return;
  if ((s = dab_ensemble_find_service(mm, p->service.sid)) == NULL) {
    tvhtrace(LS_DABEPG, "%s: unknown service %04x", mm->mm_nicename, p->service.sid);
    return;
  }

  name = p->long_name ?: p->medium_name ?: p->short_name;
  if (name == NULL)
    return;
  title = lang_str_create();
  lang_str_add(title, name, p->lang);
  if (p->short_desc) {
    summary = lang_str_create();
    lang_str_add(summary, p->short_desc, p->lang);
  }
  if (p->long_desc) {
    desc = lang_str_create();
    lang_str_add(desc, p->long_desc, p->lang);
  }
  if ((g = dab_epggrab_genre(p->genre)) != 0) {
    genre = calloc(1, sizeof(*genre));
    epg_genre_list_add_by_eit(genre, g);
  }

  LIST_FOREACH(ilm, &s->s_channels, ilm_in1_link) {
    ch = (channel_t *)ilm->ilm_in2;
    if (!ch->ch_enabled || ch->ch_epg_parent)
      continue;
    if (epg_channel_ignore_broadcast(ch, p->start))
      continue;
    save = 0;
    changes = 0;
    ebc = epg_broadcast_find_by_time(ch, ctx->mod, p->start, p->start + p->duration,
                                     1, &save, &changes);
    if (ebc == NULL)
      continue;
    save |= epg_broadcast_set_title(ebc, title, &changes);
    if (summary)
      save |= epg_broadcast_set_summary(ebc, summary, &changes);
    if (desc)
      save |= epg_broadcast_set_description(ebc, desc, &changes);
    if (genre)
      save |= epg_broadcast_set_genre(ebc, genre, &changes);
    save |= epg_broadcast_change_finish(ebc, changes, 0);
    ctx->save |= save;
    ctx->broadcasts++;
    tvhtrace(LS_DABEPG, "%s: %s %"PRItime_t" +%u '%s'", s->s_nicename,
             channel_get_name(ch, channel_blank_name), p->start, p->duration, name);
  }

  lang_str_destroy(title);
  if (summary) lang_str_destroy(summary);
  if (desc) lang_str_destroy(desc);
  if (genre) epg_genre_list_destroy(genre);
}

/* epggrab data thread */
static void dab_epggrab_process_data(void *m, void *data, uint32_t len)
{
  dab_epggrab_ctx_t ctx;
  dab_epggrab_data_t *hdr = data;
  dab_epg_contentid_t scope;
  int r;

  if (len < sizeof(*hdr))
    return;
  memset(&scope, 0, sizeof(scope));
  if (hdr->scope_id_len)
    dab_epg_decode_contentid(hdr->scope_id, hdr->scope_id_len, &scope);

  memset(&ctx, 0, sizeof(ctx));
  ctx.mod = m;
  tvh_mutex_lock(&global_lock);
  ctx.mm = idnode_find(hdr->ensemble, &dab_ensemble_class, NULL);
  if (ctx.mm && ctx.mod->enabled) {
    r = dab_epg_parse((uint8_t *)data + sizeof(*hdr), len - sizeof(*hdr),
                      &scope, dab_epggrab_programme, &ctx);
    if (r < 0)
      tvhwarn(LS_DABEPG, "%s: invalid programme information", ctx.mm->mm_nicename);
    else
      tvhdebug(LS_DABEPG, "%s: %d programmes, %d broadcasts updated",
               ctx.mm->mm_nicename, r, ctx.broadcasts);
    if (ctx.save)
      epg_updated();
  }
  tvh_mutex_unlock(&global_lock);
}

static int dab_epggrab_activate(void *m, int e)
{
  epggrab_module_t *mod = m;
  mod->active = !!e;
  return 1;
}

const idclass_t epggrab_mod_dab_class = {
  .ic_super      = &epggrab_mod_class,
  .ic_class      = "epggrab_mod_dab",
  .ic_caption    = N_("DAB EPG grabber"),
  .ic_properties = (const property_t[]){
    {}
  }
};

void dab_epggrab_init(void)
{
  epggrab_module_t *mod = calloc(1, sizeof(epggrab_module_t));

  mod->type         = EPGGRAB_DAB;
  mod->activate     = dab_epggrab_activate;
  mod->process_data = dab_epggrab_process_data;
  /* enabled by default, the data is only received while an ensemble is tuned */
  mod->enabled      = 1;
  mod->active       = 1;
  dab_epggrab_mod = epggrab_module_create(mod, &epggrab_mod_dab_class,
                                          "dab", LS_DABEPG, NULL,
                                          "DAB: SPI EPG Grabber", 2);
}

void dab_epggrab_done(void)
{
  dab_epggrab_mod = NULL;
}
