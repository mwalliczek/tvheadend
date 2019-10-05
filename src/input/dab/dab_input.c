/*
 *  Tvheadend - DAB Input
 *
 *  Copyright (C) 2019 Matthias Walliczek
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

#include "input.h"
#include "streaming.h"
#include "access.h"
#include "notify.h"
#include "dbus.h"
#include "memoryinfo.h"

static void
dab_input_del_network ( dab_network_link_t *mnl );

/*
 * DBUS
 */

static void
dab_input_dbus_notify(dab_input_t *mi, int64_t subs)
{
#if ENABLE_DBUS_1
  char buf[256], ubuf[UUID_HEX_SIZE];
  htsmsg_t *msg;

  if (mi->mi_dbus_subs == subs)
    return;
  mi->mi_dbus_subs = subs;
  msg = htsmsg_create_list();
  mi->mi_display_name(mi, buf, sizeof(buf));
  htsmsg_add_str(msg, NULL, buf);
  htsmsg_add_s64(msg, NULL, subs);
  snprintf(buf, sizeof(buf), "/input/dab/%s", idnode_uuid_as_str(&mi->ti_id, ubuf));
  dbus_emit_signal(buf, "status", msg);
#endif
}

/* **************************************************************************
 * Class definition
 * *************************************************************************/

static void
dab_input_class_get_title
  ( idnode_t *in, const char *lang, char *dst, size_t dstsize )
{
  dab_input_t *mi = (dab_input_t*)in;
  mi->mi_display_name(mi, dst, dstsize);
}

const void *
dab_input_class_active_get ( void *obj )
{
  static int active;
  dab_input_t *mi = obj;
  active = mi->mi_enabled ? 1 : 0;
  return &active;
}

const void *
dab_input_class_network_get ( void *obj )
{
  dab_network_link_t *mnl;  
  dab_input_t *mi = obj;
  htsmsg_t       *l  = htsmsg_create_list();

  LIST_FOREACH(mnl, &mi->mi_networks, mnl_mi_link)
    htsmsg_add_uuid(l, NULL, &mnl->mnl_network->mn_id.in_uuid);

  return l;
}

int
dab_input_class_network_set ( void *obj, const void *p )
{
  return dab_input_set_networks(obj, (htsmsg_t*)p);
}

htsmsg_t *
dab_input_class_network_enum ( void *obj, const char *lang )
{
  htsmsg_t *p, *m;

  if (!obj)
    return NULL;

  p = htsmsg_create_map();
  htsmsg_add_uuid(p, "uuid",    &((idnode_t*)obj)->in_uuid);
  htsmsg_add_bool(p, "enum",    1);

  m = htsmsg_create_map();
  htsmsg_add_str (m, "type",    "api");
  htsmsg_add_str (m, "uri",     "dab/input/network_list");
  htsmsg_add_str (m, "event",   "dab_network");
  htsmsg_add_msg (m, "params",  p);
  return m;
}

char *
dab_input_class_network_rend ( void *obj, const char *lang )
{
  char *str;
  dab_network_link_t *mnl;  
  dab_network_t *mn;
  dab_input_t *mi = obj;
  htsmsg_t        *l = htsmsg_create_list();
  char buf[384];

  LIST_FOREACH(mnl, &mi->mi_networks, mnl_mi_link) {
    mn = mnl->mnl_network;
    htsmsg_add_str(l, NULL, idnode_get_title(&mn->mn_id, lang, buf, sizeof(buf)));
  }

  str = htsmsg_list_2_csv(l, ',', 1);
  htsmsg_destroy(l);

  return str;
}

static void
dab_input_enabled_notify ( void *p, const char *lang )
{
  dab_input_t *mi = p;
  dab_ensemble_instance_t *mmi;

  /* Stop */
  LIST_FOREACH(mmi, &mi->mi_ensemble_active, mmi_active_link)
    mmi->mmi_ensemble->mm_stop(mmi->mmi_ensemble, 1, SM_CODE_ABORTED);

  /* Alert */
  if (mi->mi_enabled_updated)
    mi->mi_enabled_updated(mi);
}

PROP_DOC(priority)
PROP_DOC(streaming_priority)

const idclass_t dab_input_class =
{
  .ic_super      = &tvh_input_class,
  .ic_class      = "dab_input",
  .ic_caption    = N_("MPEG-TS input"),
  .ic_event      = "dab_input",
  .ic_perm_def   = ACCESS_ADMIN,
  .ic_get_title  = dab_input_class_get_title,
  .ic_properties = (const property_t[]){
    {
      .type     = PT_BOOL,
      .id       = "active",
      .name     = N_("Active"),
      .opts     = PO_RDONLY | PO_NOSAVE | PO_NOUI,
      .get      = dab_input_class_active_get,
    },
    {
      .type     = PT_BOOL,
      .id       = "enabled",
      .name     = N_("Enabled"),
      .desc     = N_("Enable/disable tuner/adapter."),
      .off      = offsetof(dab_input_t, mi_enabled),
      .notify   = dab_input_enabled_notify,
      .def.i    = 1,
    },
    {
      .type     = PT_INT,
      .id       = "priority",
      .name     = N_("Priority"),
      .desc     = N_("The tuner priority value (a higher value means to "
                     "use this tuner out of preference). See Help for details."),
      .doc      = prop_doc_priority,
      .off      = offsetof(dab_input_t, mi_priority),
      .def.i    = 1,
      .opts     = PO_ADVANCED
    },
    {
      .type     = PT_INT,
      .id       = "spriority",
      .name     = N_("Streaming priority"),
      .desc     = N_("The tuner priority value for streamed channels "
                     "through HTTP or HTSP (a higher value means to use "
                     "this tuner out of preference). If not set (zero), "
                     "the standard priority value is used. See Help for details."),
      .doc      = prop_doc_streaming_priority,
      .off      = offsetof(dab_input_t, mi_streaming_priority),
      .def.i    = 1,
      .opts     = PO_ADVANCED
    },
    {
      .type     = PT_STR,
      .id       = "displayname",
      .name     = N_("Name"),
      .desc     = N_("Name of the tuner/adapter."),
      .off      = offsetof(dab_input_t, mi_name),
      .notify   = idnode_notify_title_changed_lang,
    },
    {
      .type     = PT_BOOL,
      .id       = "ota_epg",
      .name     = N_("Over-the-air EPG"),
      .desc     = N_("Enable over-the-air program guide (EPG) scanning "
                     "on this input device."),
      .off      = offsetof(dab_input_t, mi_ota_epg),
      .def.i    = 1,
    },
    {
      .type     = PT_BOOL,
      .id       = "initscan",
      .name     = N_("Initial scan"),
      .desc     = N_("Allow the initial scan tuning on this device "
                     "(scan when Tvheadend starts or when a new multiplex "
                     "is added automatically). At least one tuner or input "
                     "should have this settings turned on. "
                     "See also 'Skip Startup Scan' in the network settings "
                     "for further details."),
      .off      = offsetof(dab_input_t, mi_initscan),
      .def.i    = 1,
      .opts     = PO_ADVANCED,
    },
    {
      .type     = PT_BOOL,
      .id       = "idlescan",
      .name     = N_("Idle scan"),
      .desc     = N_("Allow idle scan tuning on this device."),
      .off      = offsetof(dab_input_t, mi_idlescan),
      .def.i    = 1,
      .opts     = PO_ADVANCED,
    },
    {
      .type     = PT_U32,
      .id       = "free_weight",
      .name     = N_("Free subscription weight"),
      .desc     = N_("If the subscription weight for the input is below "
                     "the specified threshold, the tuner is handled as free "
                     "(according the priority settings). Otherwise, the next "
                     "tuner (without any subscriptions) is used. Set this value "
                     "to 10, if you are willing to override scan and epggrab "
                     "subscriptions."),
      .off      = offsetof(dab_input_t, mi_free_weight),
      .def.i    = 1,
      .opts     = PO_ADVANCED,
    },
    {
      .type     = PT_STR,
      .id       = "networks",
      .name     = N_("Networks"),
      .desc     = N_("Associate this device with one or more networks."),
      .islist   = 1,
      .set      = dab_input_class_network_set,
      .get      = dab_input_class_network_get,
      .list     = dab_input_class_network_enum,
      .rend     = dab_input_class_network_rend,
    },
    {}
  }
};

/* **************************************************************************
 * Class methods
 * *************************************************************************/

int
dab_input_is_enabled
  ( dab_input_t *mi, dab_ensemble_t *mm, int flags, int weight )
{
  if ((flags & SUBSCRIPTION_EPG) != 0 && !mi->mi_ota_epg)
    return MI_IS_ENABLED_NEVER;
  if ((flags & SUBSCRIPTION_USERSCAN) == 0) {
    if ((flags & SUBSCRIPTION_INITSCAN) != 0 && !mi->mi_initscan)
      return MI_IS_ENABLED_NEVER;
    if ((flags & SUBSCRIPTION_IDLESCAN) != 0 && !mi->mi_idlescan)
      return MI_IS_ENABLED_NEVER;
  }
  return mi->mi_enabled ? MI_IS_ENABLED_OK : MI_IS_ENABLED_NEVER;
}

void
dab_input_set_enabled ( dab_input_t *mi, int enabled )
{
  enabled = !!enabled;
  if (mi->mi_enabled != enabled) {
    htsmsg_t *conf = htsmsg_create_map();
    htsmsg_add_bool(conf, "enabled", enabled);
    idnode_update(&mi->ti_id, conf);
    htsmsg_destroy(conf);
  }
}

static void
dab_input_display_name ( dab_input_t *mi, char *buf, size_t len )
{
  if (mi->mi_name) {
    strlcpy(buf, mi->mi_name, len);
  } else {
    *buf = 0;
  }
}

int
dab_input_get_weight(dab_input_t *mi, dab_ensemble_t *mm, int flags, int weight )
{
  const dab_ensemble_instance_t *mmi;
  const service_t *s;
  const th_subscription_t *ths;
  int w = 0, count = 0;

  /* Service subs */
  tvh_mutex_lock(&mi->mi_output_lock);
  LIST_FOREACH(mmi, &mi->mi_ensemble_active, mmi_active_link)
    LIST_FOREACH(s, &mmi->mmi_ensemble->mm_transports, s_active_link)
      LIST_FOREACH(ths, &s->s_subscriptions, ths_service_link) {
        w = MAX(w, ths->ths_weight);
        count++;
      }
  tvh_mutex_unlock(&mi->mi_output_lock);
  return w > 0 ? w + count - 1 : 0;
}

int
dab_input_get_priority(dab_input_t *mi, dab_ensemble_t *mm, int flags )
{
  if (flags & SUBSCRIPTION_STREAMING) {
    if (mi->mi_streaming_priority > 0)
      return mi->mi_streaming_priority;
  }
  return mi->mi_priority;
}

int
dab_input_warm_ensemble ( dab_input_t *mi, dab_ensemble_instance_t *mmi )
{
  dab_ensemble_instance_t *cur;

  cur = LIST_FIRST(&mi->mi_ensemble_active);
  if (cur != NULL) {
    /* Already tuned */
    if (mmi == cur)
      return 0;

    /* Stop current */
    cur->mmi_ensemble->mm_stop(cur->mmi_ensemble, 1, SM_CODE_SUBSCRIPTION_OVERRIDDEN);
  }
  if (LIST_FIRST(&mi->mi_ensemble_active))
    return SM_CODE_TUNING_FAILED;
  return 0;
}

static int
dab_input_start_ensemble ( dab_input_t *mi, dab_ensemble_instance_t *mmi, int weight )
{
  return SM_CODE_TUNING_FAILED;
}

static void
dab_input_stop_ensemble ( dab_input_t *mi, dab_ensemble_instance_t *mmi )
{
}

void
dab_input_open_service
  ( dab_input_t *mi, dab_service_t *s, int flags, int init, int weight )
{
  dab_ensemble_t *mm = s->s_dab_ensemble;

  /* Add to list */
  tvh_mutex_lock(&mi->mi_output_lock);
  if (!s->s_dab_active_input) {
    LIST_INSERT_HEAD(&mm->mm_transports, ((service_t*)s), s_active_link);
    s->s_dab_active_input = mi;
  }
  tvh_mutex_unlock(&mi->mi_output_lock);
}

void
dab_input_close_service ( dab_input_t *mi, dab_service_t *s )
{
  /* Remove from list */
  tvh_mutex_lock(&mi->mi_output_lock);
  if (s->s_dab_active_input != NULL) {
    LIST_REMOVE(((service_t*)s), s_active_link);
    s->s_dab_active_input = NULL;
  }
  tvh_mutex_unlock(&mi->mi_output_lock);

  /* Stop ensemble? */
  s->s_dab_ensemble->mm_stop(s->s_dab_ensemble, 0, SM_CODE_OK);
}

void
dab_input_create_ensemble_instance
  ( dab_input_t *mi, dab_ensemble_t *mm )
{
  extern const idclass_t dab_ensemble_instance_class;
  tvh_input_instance_t *tii;
  LIST_FOREACH(tii, &mi->mi_ensemble_instances, tii_input_link)
    if (((dab_ensemble_instance_t *)tii)->mmi_ensemble == mm) break;
  if (!tii)
    dab_ensemble_instance_create(dab_ensemble_instance, NULL, mi, mm);
}

static void
dab_input_started_ensemble
  ( dab_input_t *mi, dab_ensemble_instance_t *mmi )
{
  dab_ensemble_t *mm = mmi->mmi_ensemble;

  /* Deliver first TS packets as fast as possible */
  atomic_set_s64(&mi->mi_last_dispatch, 0);

  /* Arm timer */
  if (LIST_FIRST(&mi->mi_ensemble_active) == NULL)
    mtimer_arm_rel(&mi->mi_status_timer, dab_input_status_timer, mi, sec2mono(1));

  /* Update */
  mm->mm_active = mmi;

  /* Accept packets */
  LIST_INSERT_HEAD(&mi->mi_ensemble_active, mmi, mmi_active_link);
  notify_reload("input_status");
  dab_input_dbus_notify(mi, 1);
}

static void
dab_input_stopping_ensemble
  ( dab_input_t *mi, dab_ensemble_instance_t *mmi )
{
  assert(mmi->mmi_ensemble->mm_active);

  tvh_mutex_lock(&mi->mi_output_lock);
  mmi->mmi_ensemble->mm_active = NULL;
  tvh_mutex_unlock(&mi->mi_output_lock);
}

static void
dab_input_stopped_ensemble
  ( dab_input_t *mi, dab_ensemble_instance_t *mmi )
{
  char buf[256];
  service_t *s, *s_next;
  dab_ensemble_t *mm = mmi->mmi_ensemble;

  /* no longer active */
  LIST_REMOVE(mmi, mmi_active_link);

  /* Disarm timer */
  if (LIST_FIRST(&mi->mi_ensemble_active) == NULL)
    mtimer_disarm(&mi->mi_status_timer);

  mi->mi_display_name(mi, buf, sizeof(buf));
  tvhtrace(LS_RTLSDR, "%s - flush subscribers", buf);
  for (s = LIST_FIRST(&mm->mm_transports); s; s = s_next) {
    s_next = LIST_NEXT(s, s_active_link);
    service_remove_subscriber(s, NULL, SM_CODE_SUBSCRIPTION_OVERRIDDEN);
  }
  notify_reload("input_status");
  dab_input_dbus_notify(mi, 0);
}

static int
dab_input_has_subscription ( dab_input_t *mi, dab_ensemble_t *mm )
{
  int ret = 0;
  const service_t *t;
  const th_subscription_t *ths;
  tvh_mutex_lock(&mi->mi_output_lock);
  LIST_FOREACH(t, &mm->mm_transports, s_active_link) {
    if (t->s_type == STYPE_RAW) {
      LIST_FOREACH(ths, &t->s_subscriptions, ths_service_link)
        if (!strcmp(ths->ths_title, "keep")) break;
      if (ths) continue;
    }
    ret = 1;
    break;
  }
  tvh_mutex_unlock(&mi->mi_output_lock);
  return ret;
}

static void
dab_input_error ( dab_input_t *mi, dab_ensemble_t *mm, int tss_flags )
{
  service_t *t, *t_next;
  tvh_mutex_lock(&mi->mi_output_lock);
  for (t = LIST_FIRST(&mm->mm_transports); t; t = t_next) {
    t_next = LIST_NEXT(t, s_active_link);
    tvh_mutex_lock(&t->s_stream_mutex);
    service_set_streaming_status_flags(t, tss_flags);
    tvh_mutex_unlock(&t->s_stream_mutex);
  }
  tvh_mutex_unlock(&mi->mi_output_lock);
}

int
dab_input_get_grace(dab_input_t *mi, dab_ensemble_t *mm)
{
	return 60;
}

static void
dab_input_stream_status
  ( dab_ensemble_instance_t *mmi, tvh_input_stream_t *st )
{
  int s = 0, w = 0;
  char buf[512], ubuf[UUID_HEX_SIZE];
  th_subscription_t *ths;
  const service_t *t;
  dab_ensemble_t *mm = mmi->mmi_ensemble;
  dab_input_t *mi = mmi->mmi_input;

  LIST_FOREACH(t, &mm->mm_transports, s_active_link)
    if (((dab_service_t *)t)->s_dab_ensemble == mm)
      LIST_FOREACH(ths, &t->s_subscriptions, ths_service_link) {
        s++;
        w = MAX(w, ths->ths_weight);
      }

  st->uuid        = strdup(idnode_uuid_as_str(&mmi->tii_id, ubuf));
  mi->mi_display_name(mi, buf, sizeof(buf));
  st->input_name  = strdup(buf);
  dab_ensemble_nice_name(mm, buf, sizeof(buf));
  st->stream_name = strdup(buf);
  st->subs_count  = s;
  st->max_weight  = w;

  tvh_mutex_lock(&mmi->tii_stats_mutex);
  st->stats.signal = mmi->tii_stats.signal;
  st->stats.snr    = mmi->tii_stats.snr;
  st->stats.ber    = mmi->tii_stats.ber;
  st->stats.signal_scale = mmi->tii_stats.signal_scale;
  st->stats.snr_scale    = mmi->tii_stats.snr_scale;
  st->stats.ec_bit   = mmi->tii_stats.ec_bit;
  st->stats.tc_bit   = mmi->tii_stats.tc_bit;
  st->stats.ec_block = mmi->tii_stats.ec_block;
  st->stats.tc_block = mmi->tii_stats.tc_block;
  tvh_mutex_unlock(&mmi->tii_stats_mutex);
  st->stats.unc   = atomic_get(&mmi->tii_stats.unc);
  st->stats.cc    = atomic_get(&mmi->tii_stats.cc);
  st->stats.te    = atomic_get(&mmi->tii_stats.te);
  st->stats.bps   = atomic_exchange(&mmi->tii_stats.bps, 0) * 8;
}

void
dab_input_empty_status
  ( dab_input_t *mi, tvh_input_stream_t *st )
{
  char buf[512], ubuf[UUID_HEX_SIZE];
  tvh_input_instance_t *mmi_;
  dab_ensemble_instance_t *mmi;

  st->uuid        = strdup(idnode_uuid_as_str(&mi->ti_id, ubuf));
  mi->mi_display_name(mi, buf, sizeof(buf));
  st->input_name  = strdup(buf);
  LIST_FOREACH(mmi_, &mi->mi_ensemble_instances, tii_input_link) {
    mmi = (dab_ensemble_instance_t *)mmi_;
    st->stats.unc += atomic_get(&mmi->tii_stats.unc);
    st->stats.cc += atomic_get(&mmi->tii_stats.cc);
    tvh_mutex_lock(&mmi->tii_stats_mutex);
    st->stats.te += mmi->tii_stats.te;
    st->stats.ec_block += mmi->tii_stats.ec_block;
    st->stats.tc_block += mmi->tii_stats.tc_block;
    tvh_mutex_unlock(&mmi->tii_stats_mutex);
  }
}

static void
dab_input_get_streams
  ( tvh_input_t *i, tvh_input_stream_list_t *isl )
{
  tvh_input_stream_t *st = NULL;
  dab_input_t *mi = (dab_input_t*)i;
  dab_ensemble_instance_t *mmi;

  tvh_mutex_lock(&mi->mi_output_lock);
  LIST_FOREACH(mmi, &mi->mi_ensemble_active, mmi_active_link) {
    st = calloc(1, sizeof(tvh_input_stream_t));
    dab_input_stream_status(mmi, st);
    LIST_INSERT_HEAD(isl, st, link);
  }
  if (st == NULL && mi->mi_empty_status && mi->mi_enabled) {
    st = calloc(1, sizeof(tvh_input_stream_t));
    mi->mi_empty_status(mi, st);
    LIST_INSERT_HEAD(isl, st, link);
  }
  tvh_mutex_unlock(&mi->mi_output_lock);
}

static void
dab_input_clear_stats ( tvh_input_t *i )
{
  dab_input_t *mi = (dab_input_t*)i;
  tvh_input_instance_t *mmi_;
  dab_ensemble_instance_t *mmi;

  tvh_mutex_lock(&mi->mi_output_lock);
  LIST_FOREACH(mmi_, &mi->mi_ensemble_instances, tii_input_link) {
    mmi = (dab_ensemble_instance_t *)mmi_;
    atomic_set(&mmi->tii_stats.unc, 0);
    atomic_set(&mmi->tii_stats.cc, 0);
    tvh_mutex_lock(&mmi->tii_stats_mutex);
    mmi->tii_stats.te = 0;
    mmi->tii_stats.ec_block = 0;
    mmi->tii_stats.tc_block = 0;
    tvh_mutex_unlock(&mmi->tii_stats_mutex);
  }
  tvh_mutex_unlock(&mi->mi_output_lock);
  notify_reload("input_status");
}



/* **************************************************************************
 * Status monitoring
 * *************************************************************************/

void
dab_input_status_timer ( void *p )
{
  tvh_input_stream_t st;
  dab_input_t *mi = p;
  dab_ensemble_instance_t *mmi;
  htsmsg_t *e;
  int64_t subs = 0;

  tvh_mutex_lock(&mi->mi_output_lock);
  LIST_FOREACH(mmi, &mi->mi_ensemble_active, mmi_active_link) {
    memset(&st, 0, sizeof(st));
    dab_input_stream_status(mmi, &st);
    e = tvh_input_stream_create_msg(&st);
    htsmsg_add_u32(e, "update", 1);
    notify_by_msg("input_status", e, 0);
    subs += st.subs_count;
    tvh_input_stream_destroy(&st);
  }
  tvh_mutex_unlock(&mi->mi_output_lock);
  mtimer_arm_rel(&mi->mi_status_timer, dab_input_status_timer, mi, sec2mono(1));
  dab_input_dbus_notify(mi, subs);
}

/* **************************************************************************
 * Creation/Config
 * *************************************************************************/

static int dab_input_idx = 0;
dab_input_list_t dab_input_all;

dab_input_t*
dab_input_create0  
  ( dab_input_t *mi, const idclass_t *class, const char *uuid,
    htsmsg_t *c )
{
  char buf[32];

  if (idnode_insert(&mi->ti_id, uuid, class, 0)) {
    if (uuid)
      tvherror(LS_RTLSDR, "invalid input uuid '%s'", uuid);
    free(mi);
    return NULL;
  }
  snprintf(buf, sizeof(buf), "input %p", mi);
  LIST_INSERT_HEAD(&tvh_inputs, (tvh_input_t*)mi, ti_link);
  
  /* Defaults */
  mi->mi_is_enabled           = dab_input_is_enabled;
  mi->mi_display_name         = dab_input_display_name;
  mi->mi_get_weight           = dab_input_get_weight;
  mi->mi_get_priority         = dab_input_get_priority;
  mi->mi_warm_ensemble             = dab_input_warm_ensemble;
  mi->mi_start_ensemble            = dab_input_start_ensemble;
  mi->mi_stop_ensemble             = dab_input_stop_ensemble;
  mi->mi_open_service         = dab_input_open_service;
  mi->mi_close_service        = dab_input_close_service;
  mi->mi_create_ensemble_instance  = dab_input_create_ensemble_instance;
  mi->mi_started_ensemble          = dab_input_started_ensemble;
  mi->mi_stopping_ensemble         = dab_input_stopping_ensemble;
  mi->mi_stopped_ensemble          = dab_input_stopped_ensemble;
  mi->mi_has_subscription     = dab_input_has_subscription;
  mi->mi_error                = dab_input_error;
  mi->ti_get_streams          = dab_input_get_streams;
  mi->ti_clear_stats          = dab_input_clear_stats;

  /* Index */
  mi->mi_instance             = ++dab_input_idx;

  tvh_mutex_init(&mi->mi_output_lock, NULL);

  /* Defaults */
  mi->mi_ota_epg = 1;
  mi->mi_initscan = 1;
  mi->mi_idlescan = 1;

  /* Add to global list */
  LIST_INSERT_HEAD(&dab_input_all, mi, mi_global_link);

  /* Load config */
  if (c)
    idnode_load(&mi->ti_id, c);

  return mi;
}

void
dab_input_stop_all ( dab_input_t *mi )
{
  dab_ensemble_instance_t *mmi;
  while ((mmi = LIST_FIRST(&mi->mi_ensemble_active)))
    mmi->mmi_ensemble->mm_stop(mmi->mmi_ensemble, 1, SM_CODE_OK);
}

void
dab_input_delete ( dab_input_t *mi, int delconf )
{
  dab_network_link_t *mnl;
  tvh_input_instance_t *tii, *tii_next;

  /* Early shutdown flag */
  atomic_set(&mi->mi_running, 0);

  idnode_save_check(&mi->ti_id, delconf);

  /* Remove networks */
  while ((mnl = LIST_FIRST(&mi->mi_networks)))
    dab_input_del_network(mnl);

  /* Remove ensemble instances assigned to this input */
  tii = LIST_FIRST(&mi->mi_ensemble_instances);
  while (tii) {
    tii_next = LIST_NEXT(tii, tii_input_link);
    if (((dab_ensemble_instance_t *)tii)->mmi_input == mi)
      tii->tii_delete(tii);
    tii = tii_next;
  }

  /* Remove global refs */
  idnode_unlink(&mi->ti_id);
  LIST_REMOVE(mi, ti_link);
  LIST_REMOVE(mi, mi_global_link);

  tvh_mutex_destroy(&mi->mi_output_lock);
  free(mi->mi_name);
  free(mi->mi_linked);
  free(mi);
}

void
dab_input_save ( dab_input_t *mi, htsmsg_t *m )
{
  idnode_save(&mi->ti_id, m);
}

int
dab_input_add_network ( dab_input_t *mi, dab_network_t *mn )
{
  dab_network_link_t *mnl;

  /* Find existing */
  LIST_FOREACH(mnl, &mi->mi_networks, mnl_mi_link)
    if (mnl->mnl_network == mn)
      break;

  /* Clear mark */
  if (mnl) {
    mnl->mnl_mark = 0;
    return 0;
  }

  /* Create new */
  mnl = calloc(1, sizeof(dab_network_link_t));
  mnl->mnl_input    = mi;
  mnl->mnl_network  = mn;
  LIST_INSERT_HEAD(&mi->mi_networks, mnl, mnl_mi_link);
  LIST_INSERT_HEAD(&mn->mn_inputs,   mnl, mnl_mn_link);
  idnode_notify_changed(&mnl->mnl_network->mn_id);
  return 1;
}

static void
dab_input_del_network ( dab_network_link_t *mnl )
{
  idnode_notify_changed(&mnl->mnl_network->mn_id);
  LIST_REMOVE(mnl, mnl_mn_link);
  LIST_REMOVE(mnl, mnl_mi_link);
  free(mnl);
}

int
dab_input_set_networks ( dab_input_t *mi, htsmsg_t *msg )
{
  int save = 0;
  const char *str;
  htsmsg_field_t *f;
  dab_network_t *mn;
  dab_network_link_t *mnl, *nxt;

  /* Mark for deletion */
  LIST_FOREACH(mnl, &mi->mi_networks, mnl_mi_link)
    mnl->mnl_mark = 1;

  /* Link */
  HTSMSG_FOREACH(f, msg) {
    if (!(str = htsmsg_field_get_str(f))) continue;
    if (!(mn = dab_network_find(str))) continue;
    save |= dab_input_add_network(mi, mn);
  }

  /* Unlink */
  for (mnl = LIST_FIRST(&mi->mi_networks); mnl != NULL; mnl = nxt) {
    nxt = LIST_NEXT(mnl, mnl_mi_link);
    if (mnl->mnl_mark) {
      dab_input_del_network(mnl);
      save = 1;
    }
  }
  
  return save;
}

int
dab_input_grace( dab_input_t *mi, dab_ensemble_t *mm )
{
  /* Get timeout */
  int t = 0;
  if (mi && mi->mi_get_grace)
    t = mi->mi_get_grace(mi, mm);
  if (t < 5) t = 5; // lower bound
  return t;
}

/******************************************************************************
 * Wizard
 *****************************************************************************/

htsmsg_t *
dab_network_wizard_get
  ( dab_input_t *mi, const idclass_t *idc,
    dab_network_t *mn, const char *lang )
{
  htsmsg_t *m = htsmsg_create_map(), *l, *e;
  char buf[256];

  if (mi && idc) {
    mi->mi_display_name(mi, buf, sizeof(buf));
    htsmsg_add_str(m, "input_name", buf);
    l = htsmsg_create_list();
    e = htsmsg_create_key_val(idc->ic_class, idclass_get_caption(idc, lang));
    htsmsg_add_msg(l, NULL, e);
    htsmsg_add_msg(m, "dab_network_types", l);
    if (mn)
      htsmsg_add_uuid(m, "dab_network", &mn->mn_id.in_uuid);
  }
  return m;
}

