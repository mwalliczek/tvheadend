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
 *
 * The scheduler tunes the ensembles that carry an EPG (or were not
 * checked yet) one after the other at the configured times, with a low
 * priority subscription: it stays on an ensemble until no new object
 * arrived for a while (the carousel is complete), at most for the
 * configured time.
 */

#include <string.h>

#include "tvheadend.h"
#include "atomic.h"
#include "cron.h"
#include "subscriptions.h"
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

typedef struct dab_epggrab_module {
  epggrab_module_t;
  char     *sched_cron;
  int       sched_initial;
  uint32_t  sched_timeout;            /* minutes per ensemble */
} dab_epggrab_module_t;

static dab_epggrab_module_t *dab_epggrab_mod;

int dab_epggrab_enabled(void)
{
  return dab_epggrab_mod && dab_epggrab_mod->enabled;
}

/* ************************************************************************
 * Scheduler
 * ***********************************************************************/

#define SCHED_SUBSCRIPTION  "epggrab"
#define SCHED_TICK          5         /* s */
#define SCHED_NO_EPG        30        /* s in sync without EPG component */
#define SCHED_NO_SIGNAL     60        /* s without FIC sync */
#define SCHED_IDLE          120       /* s without a new object: complete */
#define SCHED_WAIT_TUNER    3600      /* s to wait for a free tuner */
#define SCHED_INITIAL_DELAY 120       /* s after the start of tvheadend */

static struct {
  mtimer_t          tick;
  mtimer_t          initial;
  gtimer_t          cron_timer;
  cron_multi_t     *cron;
  char            (*queue)[UUID_HEX_SIZE];
  int               queue_len;
  int               queue_pos;
  int64_t           run_start;
  /* the ensemble being grabbed */
  char              cur[UUID_HEX_SIZE];
  int               subscribed;
  int64_t           start;
  int64_t           synced;
  int64_t           retry;
  dab_ensemble_t   *mm;               /* read by the demodulator thread */
  volatile int      objects;
  volatile int64_t  last_object;
} sched;

static void dab_epggrab_sched_tick(void *aux);

static int dab_epggrab_sched_running(void)
{
  return sched.queue != NULL;
}

static void dab_epggrab_sched_release(dab_ensemble_t *mm)
{
  sched.mm = NULL;
  if (mm && sched.subscribed)
    dab_ensemble_unsubscribe_by_name(mm, SCHED_SUBSCRIPTION);
  sched.subscribed = 0;
  sched.cur[0] = '\0';
}

static void dab_epggrab_sched_stop(const char *reason)
{
  dab_ensemble_t *mm = NULL;

  if (!dab_epggrab_sched_running())
    return;
  if (sched.cur[0])
    mm = idnode_find(sched.cur, &dab_ensemble_class, NULL);
  dab_epggrab_sched_release(mm);
  tvhinfo(LS_DABEPG, "EPG grab %s", reason);
  free(sched.queue);
  sched.queue = NULL;
  sched.queue_len = sched.queue_pos = 0;
  mtimer_disarm(&sched.tick);
}

/* queue all enabled ensembles which carry an EPG or were not checked yet */
static void dab_epggrab_sched_start(const char *why)
{
  dab_network_t *mn;
  dab_ensemble_t *mm;
  int n = 0;

  lock_assert(&global_lock);
  if (!dab_epggrab_enabled() || dab_epggrab_sched_running())
    return;
  LIST_FOREACH(mn, &dab_network_all, mn_global_link)
    LIST_FOREACH(mm, &mn->mn_ensembles, mm_network_link)
      n++;
  if (n == 0)
    return;
  sched.queue = calloc(n, sizeof(*sched.queue));
  sched.queue_len = 0;
  LIST_FOREACH(mn, &dab_network_all, mn_global_link) {
    if (!mn->mn_enabled)
      continue;
    LIST_FOREACH(mm, &mn->mn_ensembles, mm_network_link)
      if (mm->mm_is_enabled(mm) && mm->mm_epg != DAB_EPG_NO)
        idnode_uuid_as_str(&mm->mm_id, sched.queue[sched.queue_len++]);
  }
  if (sched.queue_len == 0) {
    free(sched.queue);
    sched.queue = NULL;
    return;
  }
  sched.queue_pos = 0;
  sched.cur[0] = '\0';
  sched.run_start = mclk();
  tvhinfo(LS_DABEPG, "EPG grab started (%s), %d ensembles", why, sched.queue_len);
  mtimer_arm_rel(&sched.tick, dab_epggrab_sched_tick, NULL, sec2mono(1));
}

static void dab_epggrab_sched_done(dab_ensemble_t *mm, const char *result)
{
  tvhinfo(LS_DABEPG, "%s: EPG grab %s, %d objects in %"PRId64" s",
          mm->mm_nicename, result, atomic_get(&sched.objects),
          mono2sec(mclk() - sched.start));
  dab_epggrab_sched_release(mm);
}

static void dab_epggrab_sched_tick(void *aux)
{
  dab_ensemble_t *mm;
  int r, has_epg, sub, address, subChId, timeout;
  int64_t now = mclk();

  if (!dab_epggrab_sched_running())
    return;
  if (!dab_epggrab_enabled()) {
    dab_epggrab_sched_stop("stopped (grabber disabled)");
    return;
  }
  mtimer_arm_rel(&sched.tick, dab_epggrab_sched_tick, NULL, sec2mono(SCHED_TICK));

  /* next ensemble */
  if (sched.cur[0] == '\0') {
    if (sched.queue_pos >= sched.queue_len) {
      dab_epggrab_sched_stop("finished");
      return;
    }
    strcpy(sched.cur, sched.queue[sched.queue_pos++]);
    sched.subscribed = 0;
    sched.retry = 0;
  }
  mm = idnode_find(sched.cur, &dab_ensemble_class, NULL);
  if (mm == NULL || !mm->mm_is_enabled(mm) || !mm->mm_network->mn_enabled) {
    dab_epggrab_sched_release(NULL);
    return;
  }

  if (!sched.subscribed) {
    if (sched.retry > now)
      return;
    r = dab_ensemble_subscribe(mm, NULL, SCHED_SUBSCRIPTION, SUBSCRIPTION_PRIO_EPG,
                               SUBSCRIPTION_ONESHOT | SUBSCRIPTION_TABLES);
    if (r == SM_CODE_NO_FREE_ADAPTER || r == SM_CODE_NO_ADAPTERS) {
      /* all tuners busy: wait, but not forever */
      if (now - sched.run_start > sec2mono(SCHED_WAIT_TUNER)) {
        dab_epggrab_sched_stop("stopped (no free tuner)");
        return;
      }
      tvhtrace(LS_DABEPG, "%s: EPG grab waits for a free tuner", mm->mm_nicename);
      sched.retry = now + sec2mono(60);
      return;
    }
    if (r) {
      tvhwarn(LS_DABEPG, "%s: EPG grab cannot tune (%s)", mm->mm_nicename,
              streaming_code2txt(r));
      dab_epggrab_sched_release(NULL);
      return;
    }
    tvhdebug(LS_DABEPG, "%s: EPG grab tuning", mm->mm_nicename);
    sched.subscribed = 1;
    sched.start = now;
    sched.synced = 0;
    atomic_set(&sched.objects, 0);
    atomic_set_s64(&sched.last_object, now);
    sched.mm = mm;
    return;
  }

  /* a more important subscription took the tuner */
  sub = 0;
  {
    th_subscription_t *s;
    LIST_FOREACH(s, &mm->mm_raw_subs, ths_mux_link)
      if (s->ths_title && !strcmp(s->ths_title, SCHED_SUBSCRIPTION))
        sub = 1;
  }
  if (!sub) {
    dab_epggrab_sched_done(mm, "interrupted");
    return;
  }

  if (sched.synced == 0 && mm->mm_active && mm->mm_active->fibProcessorIsSynced)
    sched.synced = now;
  tvh_mutex_lock(&mm->mm_tables_lock);
  has_epg = dab_ensemble_find_epg_component(mm, &subChId, &address);
  tvh_mutex_unlock(&mm->mm_tables_lock);

  timeout = (dab_epggrab_mod->sched_timeout ?: 15) * 60;
  if (!has_epg) {
    if (sched.synced && now - sched.synced > sec2mono(SCHED_NO_EPG)) {
      dab_ensemble_set_epg(mm, DAB_EPG_NO);
      dab_epggrab_sched_done(mm, "skipped (no EPG)");
    } else if (!sched.synced && now - sched.start > sec2mono(SCHED_NO_SIGNAL)) {
      dab_epggrab_sched_done(mm, "skipped (no reception)");
    }
    return;
  }
  if (atomic_get(&sched.objects) > 0 &&
      now - atomic_get_s64(&sched.last_object) > sec2mono(SCHED_IDLE))
    dab_epggrab_sched_done(mm, "finished");
  else if (now - sched.start > sec2mono(timeout))
    dab_epggrab_sched_done(mm, "finished (time limit)");
}

static void dab_epggrab_sched_cron_arm(void);

static void dab_epggrab_sched_cron_cb(void *aux)
{
  dab_epggrab_sched_start("scheduled");
  dab_epggrab_sched_cron_arm();
}

static void dab_epggrab_sched_cron_arm(void)
{
  time_t next;

  gtimer_disarm(&sched.cron_timer);
  if (sched.cron == NULL)
    return;
  if (cron_multi_next(sched.cron, gclk(), &next)) {
    tvhwarn(LS_DABEPG, "EPG grab cron config invalid");
    return;
  }
  tvhdebug(LS_DABEPG, "next EPG grab in %"PRId64" s", (int64_t)(next - gclk()));
  gtimer_arm_absn(&sched.cron_timer, dab_epggrab_sched_cron_cb, NULL, next);
}

static void dab_epggrab_sched_set_cron(void)
{
  free(sched.cron);
  sched.cron = NULL;
  if (dab_epggrab_mod && dab_epggrab_mod->sched_cron && dab_epggrab_mod->sched_cron[0])
    sched.cron = cron_multi_set(dab_epggrab_mod->sched_cron);
  dab_epggrab_sched_cron_arm();
}

static void dab_epggrab_sched_initial_cb(void *aux)
{
  if (dab_epggrab_mod == NULL)
    return;
  dab_epggrab_sched_set_cron();
  if (dab_epggrab_mod->sched_initial)
    dab_epggrab_sched_start("after start");
}

/* demodulator thread: count the new objects of the ensemble being grabbed */
static void dab_epggrab_sched_object(dab_ensemble_t *mm)
{
  if (mm != sched.mm)
    return;
  atomic_add(&sched.objects, 1);
  atomic_set_s64(&sched.last_object, mclk());
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
  dab_epggrab_sched_object(mm);
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
  epggrab_queue_data((epggrab_module_t *)dab_epggrab_mod, &hdr, sizeof(hdr), obj->body, obj->body_len);
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
  if (!e)
    dab_epggrab_sched_stop("stopped (grabber disabled)");
  return 1;
}

static void dab_epggrab_class_cron_notify(void *self, const char *lang)
{
  if (self == dab_epggrab_mod)
    dab_epggrab_sched_set_cron();
}

static void dab_epggrab_done_mod(void *m)
{
  tvh_mutex_lock(&global_lock);
  dab_epggrab_sched_stop("stopped");
  mtimer_disarm(&sched.initial);
  gtimer_disarm(&sched.cron_timer);
  free(sched.cron);
  sched.cron = NULL;
  dab_epggrab_mod = NULL;
  tvh_mutex_unlock(&global_lock);
  free(((dab_epggrab_module_t *)m)->sched_cron);
}

const idclass_t epggrab_mod_dab_class = {
  .ic_super      = &epggrab_mod_class,
  .ic_class      = "epggrab_mod_dab",
  .ic_caption    = N_("DAB EPG grabber"),
  .ic_properties = (const property_t[]){
    {
      .type   = PT_STR,
      .id     = "cron",
      .name   = N_("Cron multi-line"),
      .desc   = N_("When the DAB ensembles with an EPG are tuned to "
                   "receive it (cron time specification, one per line). "
                   "Leave empty to receive the EPG only while a DAB "
                   "service is used."),
      .off    = offsetof(dab_epggrab_module_t, sched_cron),
      .notify = dab_epggrab_class_cron_notify,
      .opts   = PO_MULTILINE | PO_ADVANCED,
      .group  = 1,
    },
    {
      .type   = PT_BOOL,
      .id     = "initial",
      .name   = N_("Grab after start"),
      .desc   = N_("Tune the DAB ensembles with an EPG two minutes "
                   "after the start of tvheadend."),
      .off    = offsetof(dab_epggrab_module_t, sched_initial),
      .opts   = PO_ADVANCED,
      .group  = 1,
    },
    {
      .type   = PT_U32,
      .id     = "timeout",
      .name   = N_("Time limit per ensemble (minutes)"),
      .desc   = N_("The EPG grab moves on to the next ensemble when no "
                   "new EPG data arrived for two minutes, at the latest "
                   "after this time."),
      .off    = offsetof(dab_epggrab_module_t, sched_timeout),
      .opts   = PO_ADVANCED,
      .group  = 1,
    },
    {}
  }
};

void dab_epggrab_init(void)
{
  dab_epggrab_module_t *mod = calloc(1, sizeof(dab_epggrab_module_t));

  mod->type         = EPGGRAB_DAB;
  mod->activate     = dab_epggrab_activate;
  mod->process_data = dab_epggrab_process_data;
  /* enabled by default, the data is only received while an ensemble is tuned */
  mod->done         = dab_epggrab_done_mod;
  mod->enabled      = 1;
  mod->active       = 1;
  mod->sched_cron    = strdup("# Default config (03:14 every day)\n14 3 * * *");
  mod->sched_initial = 1;
  mod->sched_timeout = 15;
  dab_epggrab_mod = (dab_epggrab_module_t *)
    epggrab_module_create((epggrab_module_t *)mod, &epggrab_mod_dab_class,
                          "dab", LS_DABEPG, NULL, "DAB: SPI EPG Grabber", 2);
  /* the configuration is loaded later */
  mtimer_arm_rel(&sched.initial, dab_epggrab_sched_initial_cb, NULL,
                 sec2mono(SCHED_INITIAL_DELAY));
}

void dab_epggrab_done(void)
{
  dab_epggrab_mod = NULL;
}
