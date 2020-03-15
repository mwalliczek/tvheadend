/*
 *  DAB based service
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

#include <assert.h>

#include "tvheadend.h"
#include "service.h"
#include "channels.h"
#include "input.h"
#include "settings.h"
#include "config.h"
#include "epggrab.h"

/* **************************************************************************
 * Class definition
 * *************************************************************************/

extern const idclass_t service_class;

static const void *
dab_service_class_get_ensemble ( void *ptr )
{
  static char buf[512], *s = buf;
  dab_service_t *ms = ptr;
  if (ms->s_dab_ensemble && ms->s_dab_ensemble->mm_display_name)
    ms->s_dab_ensemble->mm_display_name(ms->s_dab_ensemble, buf, sizeof(buf));
  else
    *buf = 0;
  return &s;
}

static const void *
dab_service_class_get_ensemble_uuid ( void *ptr )
{
  dab_service_t *ms = ptr;
  if (ms && ms->s_dab_ensemble)
    idnode_uuid_as_str(&ms->s_dab_ensemble->mm_id, prop_sbuf);
  else
    prop_sbuf[0] = '\0';
  return &prop_sbuf_ptr;
}

static const void *
dab_service_class_get_network ( void *ptr )
{
  static char buf[512], *s = buf;
  dab_service_t *ms = ptr;
  dab_network_t *mn = ms->s_dab_ensemble ? ms->s_dab_ensemble->mm_network : NULL;
  if (mn && mn->mn_display_name)
    mn->mn_display_name(mn, buf, sizeof(buf));
  else
    *buf = 0;
  return &s;
}

CLASS_DOC(dab_service)

const idclass_t dab_service_class =
{
  .ic_super      = &service_class,
  .ic_class      = "dab_service",
  .ic_caption    = N_("DAB Inputs - Services"),
  .ic_doc        = tvh_doc_dab_service_class,
  .ic_order      = "enabled,channel,svcname",
  .ic_properties = (const property_t[]){
    {
      .type     = PT_STR,
      .id       = "network",
      .name     = N_("Network"),
      .desc     = N_("The network the service is on."),
      .opts     = PO_RDONLY | PO_NOSAVE,
      .get      = dab_service_class_get_network,
    },
    {
      .type     = PT_STR,
      .id       = "ensemble",
      .name     = N_("Ensemble"),
      .desc     = N_("The ensemble the service is on."),
      .opts     = PO_RDONLY | PO_NOSAVE,
      .get      = dab_service_class_get_ensemble,
    },
    {
      .type     = PT_STR,
      .id       = "ensemble_uuid",
      .name     = N_("Ensemble UUID"),
      .desc     = N_("The ensemble's universally unique identifier."),
      .opts     = PO_RDONLY | PO_NOSAVE | PO_HIDDEN | PO_EXPERT,
      .get      = dab_service_class_get_ensemble_uuid,
    },
    {
      .type     = PT_U16,
      .id       = "sid",
      .name     = N_("Service ID"),
      .desc     = N_("The service ID as set by the provider."),
      .opts     = PO_RDONLY | PO_ADVANCED,
      .off      = offsetof(dab_service_t, s_components.set_service_id),
    },
    {
      .type     = PT_STR,
      .id       = "svcname",
      .name     = N_("Service name"),
      .desc     = N_("The service name as set by the provider."),
      .opts     = PO_RDONLY,
      .off      = offsetof(dab_service_t, s_dab_svcname),
    },
    {
      .type     = PT_STR,
      .id       = "provider",
      .name     = N_("Provider"),
      .desc     = N_("The provider's name."),
      .opts     = PO_RDONLY | PO_HIDDEN,
      .off      = offsetof(dab_service_t, s_dab_provider),
    },
    {
      .type     = PT_TIME,
      .id       = "created",
      .name     = N_("Created"),
      .desc     = N_("When the service was first identified and recorded."),
      .off      = offsetof(dab_service_t, s_dab_created),
      .opts     = PO_ADVANCED | PO_RDONLY,
    },
    {
      .type     = PT_TIME,
      .id       = "last_seen",
      .name     = N_("Last seen"),
      .desc     = N_("When the service was last seen during a ensemble scan."),
      .off      = offsetof(dab_service_t, s_dab_last_seen),
      .opts     = PO_ADVANCED | PO_RDONLY,
    },
    {
      .type     = PT_INT,
      .id       = "TMid",
      .name     = N_("Transport Mechanism Identifier"),
      .desc     = N_("The transport mode."),
      .opts     = PO_RDONLY | PO_ADVANCED,
      .off      = offsetof(dab_service_t, TMid),
    },
    {
      .type     = PT_INT,
      .id       = "ASCTy",
      .name     = N_("Audio Service Component Type"),
      .desc     = N_("The type of the audio service component."),
      .opts     = PO_RDONLY | PO_ADVANCED,
      .off      = offsetof(dab_service_t, ASCTy),
    },
    {
      .type     = PT_INT,
      .id       = "PS_flag",
      .name     = N_("Primary/Secondary"),
      .desc     = N_("Indicate whether the service component is the primary one."),
      .opts     = PO_RDONLY | PO_ADVANCED,
      .off      = offsetof(dab_service_t, PS_flag),
    },
    {
      .type     = PT_INT,
      .id       = "subChId",
      .name     = N_("Sub-channel Identifier"),
      .desc     = N_("The sub-channel in which the service component is carried."),
      .opts     = PO_RDONLY | PO_ADVANCED,
      .off      = offsetof(dab_service_t, subChId),
    },
    {
      .type     = PT_INT,
      .id       = "FEC_scheme",
      .name     = N_("Forward Error Correction"),
      .desc     = N_("Forward Error Correcting scheme applied to sub-channels carrying packet mode service components."),
      .opts     = PO_RDONLY | PO_ADVANCED,
      .off      = offsetof(dab_service_t, FEC_scheme),
    },
    {},
  }
};

/* **************************************************************************
 * Class methods
 * *************************************************************************/

/*
 * Check the service is enabled
 */
static int
dab_service_is_enabled(service_t *t, int flags)
{
  dab_service_t *s = (dab_service_t*)t;
  dab_ensemble_t *mm    = s->s_dab_ensemble;
  if (!s->s_verified) return 0;
  return mm->mm_is_enabled(mm) ? s->s_enabled : 0;
}

static int
dab_service_is_enabled_raw(service_t *t, int flags)
{
  dab_service_t *s = (dab_service_t*)t;
  dab_ensemble_t *mm    = s->s_dab_ensemble;
  return mm->mm_is_enabled(mm) ? s->s_enabled : 0;
}

/*
 * Save
 */
static htsmsg_t *
dab_service_config_save ( service_t *t, char *filename, size_t fsize )
{
  if (filename == NULL) {
    htsmsg_t *e = htsmsg_create_map();
    service_save(t, e);
    return e;
  }
  idnode_changed(&((dab_service_t *)t)->s_dab_ensemble->mm_id);
  return NULL;
}

void dab_service_set_subchannel(dab_service_t *s, int16_t SubChId)
{
  elementary_set_t *set = &s->s_components;
  streaming_component_type_t hts_stream_type;
  elementary_stream_t *st, *next;
  
  s->subChId = SubChId;
  
  /* Mark all streams for deletion */
  TAILQ_FOREACH(st, &set->set_all, es_link) {
    st->es_delete_me = 1;
  }
  
  hts_stream_type = SCT_AAC;
  
  st = elementary_stream_find(set, SubChId);
  if (st == NULL || st->es_type != hts_stream_type) {
    st = elementary_stream_create(set, SubChId, hts_stream_type);
  }

  if (st->es_type != hts_stream_type) {
    st->es_type = hts_stream_type;
  }

  st->es_delete_me = 0;
  
  /* Scan again to see if any streams should be deleted */
  for(st = TAILQ_FIRST(&set->set_all); st != NULL; st = next) {
    next = TAILQ_NEXT(st, es_link);

    if(st->es_delete_me) {
      elementary_set_stream_destroy(set, st);
    }
  }
}

/*
 * Service instance list
 */
static int
dab_service_enlist_raw
  ( service_t *t, tvh_input_t *ti, struct service_instance_list *sil,
    int flags, int weight )
{
  int p, w, r, added = 0, errcnt = 0;
  dab_service_t      *s = (dab_service_t*)t;
  dab_input_t        *mi;
  dab_ensemble_t          *m = s->s_dab_ensemble;
  dab_ensemble_instance_t *mmi;

  assert(s->s_source_type == S_DAB);

  /* Create instances */
  m->mm_create_instances(m);

  /* Enlist available */
  LIST_FOREACH(mmi, &m->mm_instances, mmi_ensemble_link) {
    if (mmi->mmi_tune_failed)
      continue;

    mi = mmi->mmi_input;

    if (ti && (tvh_input_t *)mi != ti)
      continue;

    r = mi->mi_is_enabled(mi, mmi->mmi_ensemble, flags, weight);
    if (r == MI_IS_ENABLED_NEVER) {
      tvhtrace(LS_RTLSDR, "enlist: input %p not enabled for ensemble %p service %s weight %d flags %x",
                          mi, mmi->mmi_ensemble, s->s_nicename, weight, flags);
      continue;
    }
    if (r == MI_IS_ENABLED_RETRY) {
      /* temporary error - retry later */
      tvhtrace(LS_RTLSDR, "enlist: input %p postponed for ensemble %p service %s weight %d flags %x",
                          mi, mmi->mmi_ensemble, s->s_nicename, weight, flags);
      errcnt++;
      continue;
    }

    /* Set weight to -1 (forced) for already active ensemble */
    if (mmi->mmi_ensemble->mm_active == mmi) {
      w = -1;
      p = -1;
    } else {
      w = mi->mi_get_weight(mi, mmi->mmi_ensemble, flags, weight);
      p = mi->mi_get_priority(mi, mmi->mmi_ensemble, flags);
      if (w > 0 && mi->mi_free_weight &&
          weight >= mi->mi_free_weight && w < mi->mi_free_weight)
        w = 0;
    }

    service_instance_add(sil, t, mi->mi_instance, mi->mi_name, p, w);
    added++;
  }

  return added ? 0 : (errcnt ? SM_CODE_NO_FREE_ADAPTER : 0);
}

/*
 * Start service
 */
static int
dab_service_start(service_t *t, int instance, int weight, int flags)
{
  int r;
  dab_service_t      *s = (dab_service_t*)t;
  dab_ensemble_t          *m = s->s_dab_ensemble;
  dab_ensemble_instance_t *mmi;

  tvhdebug(LS_RTLSDR, "%s - start service", s->s_dab_svcname);

  /* Validate */
  assert(s->s_status      == SERVICE_IDLE);
  assert(s->s_source_type == S_DAB);
  lock_assert(&global_lock);

  /* Find */
  LIST_FOREACH(mmi, &m->mm_instances, mmi_ensemble_link)
    if (mmi->mmi_input->mi_instance == instance)
      break;
  assert(mmi != NULL);
  if (mmi == NULL)
    return SM_CODE_UNDEFINED_ERROR;

  /* Start Mux */
  mmi->mmi_start_weight = weight;
  r = dab_ensemble_instance_start(&mmi, t, weight);

  /* Start */
  if (!r) {

    /* Open service */
    s->s_dab_subscription_flags = flags;
    s->s_dab_subscription_weight = weight;
    mmi->mmi_input->mi_open_service(mmi->mmi_input, s, flags, 1, weight);
  }

  return r;
}

/*
 * Stop service
 */
static void
dab_service_stop(service_t *t)
{
  dab_service_t *s  = (dab_service_t*)t;
  dab_input_t   *i  = s->s_dab_active_input;

  tvhdebug(LS_RTLSDR, "%s - stop service", s->s_dab_svcname);

  /* Validate */
  assert(s->s_source_type == S_DAB);
  lock_assert(&global_lock);

  /* Stop */
  if (i)
    i->mi_close_service(i, s);

  /* Save some memory */
  sbuf_free(&s->s_tsbuf);
}

/*
 * Refresh
 */
static void
dab_service_refresh(service_t *t)
{
  dab_service_t *s = (dab_service_t*)t;
  dab_input_t   *i = s->s_dab_active_input;

  /* Validate */
  assert(s->s_source_type == S_DAB);
  assert(i != NULL);
  lock_assert(&global_lock);

  /* Re-open */
  i->mi_open_service(i, s, s->s_dab_subscription_flags, 0, s->s_dab_subscription_weight);
}

/*
 * Source info
 */
static void
dab_service_setsourceinfo(service_t *t, source_info_t *si)
{
  char buf[256];
  dab_service_t      *s = (dab_service_t*)t;
  dab_ensemble_t          *m = s->s_dab_ensemble;

  /* Validate */
  assert(s->s_source_type == S_DAB);

  /* Update */
  memset(si, 0, sizeof(struct source_info));
  si->si_type = S_DAB;

  uuid_duplicate(&si->si_network_uuid, &m->mm_network->mn_id.in_uuid);
  uuid_duplicate(&si->si_mux_uuid, &m->mm_id.in_uuid);

  if(m->mm_network->mn_network_name != NULL)
    si->si_network = strdup(m->mm_network->mn_network_name);
  dab_network_get_type_str(m->mm_network, buf, sizeof(buf));
  si->si_network_type = strdup(buf);

  m->mm_display_name(m, buf, sizeof(buf));
  si->si_mux = strdup(buf);

  if(s->s_dab_active_input) {
    dab_input_t *mi = s->s_dab_active_input;
    uuid_duplicate(&si->si_adapter_uuid, &mi->ti_id.in_uuid);
    mi->mi_display_name(mi, buf, sizeof(buf));
    si->si_adapter = strdup(buf);
  }

  if(s->s_dab_provider != NULL)
    si->si_provider = strdup(s->s_dab_provider);

  if(s->s_dab_svcname != NULL)
    si->si_service = strdup(s->s_dab_svcname);

}

/*
 * Grace period
 */
static int
dab_service_grace_period(service_t *t)
{
  int r = 0;
  dab_service_t *ms = (dab_service_t*)t;
  dab_ensemble_t     *mm = ms->s_dab_ensemble;
  dab_input_t   *mi = ms->s_dab_active_input;
  assert(mi != NULL);

  if (mi && mi->mi_get_grace)
    r = mi->mi_get_grace(mi, mm);
  
  return r ?: 10;  
}

static const char *
dab_service_channel_name ( service_t *s )
{
  return ((dab_service_t*)s)->s_dab_svcname;
}

static const char *
dab_service_source ( service_t *s )
{
  return "DAB";
}

static const char *
dab_service_provider_name ( service_t *s )
{
  return ((dab_service_t*)s)->s_dab_provider;
}

static const char *
dab_service_channel_icon ( service_t *s )
{
  return NULL;
}

void
dab_service_unref ( service_t *t )
{
  dab_service_t *ms = (dab_service_t*)t;

  free(ms->s_dab_svcname);
  free(ms->s_dab_provider);
}

void
dab_service_delete ( service_t *t, int delconf )
{
  dab_service_t *ms = (dab_service_t*)t;
  dab_ensemble_t     *mm = t->s_type == STYPE_STD ? ms->s_dab_ensemble : NULL;

  if (mm)
    idnode_changed(&mm->mm_id);

  /* Free memory */
  if (t->s_type == STYPE_STD)
    LIST_REMOVE(ms, s_dab_ensemble_link);
  sbuf_free(&ms->s_tsbuf);

  // Note: the ultimate deletion and removal from the idnode list
  //       is done in service_destroy
}

static void
dab_service_memoryinfo ( service_t *t, int64_t *size )
{
  dab_service_t *ms = (dab_service_t*)t;
  *size += sizeof(*ms);
  *size += tvh_strlen(ms->s_nicename);
  *size += tvh_strlen(ms->s_dab_svcname);
  *size += tvh_strlen(ms->s_dab_provider);
}

static int
dab_service_unseen( service_t *t, const char *type, time_t before )
{
  dab_service_t *ms = (dab_service_t*)t;
  int pat = type && strcasecmp(type, "pat") == 0;
  if (pat && ms->s_auto != SERVICE_AUTO_PAT_MISSING) return 0;
  return ms->s_dab_last_seen < before;
}

/* **************************************************************************
 * Creation/Location
 * *************************************************************************/

/*
 * Create service
 */
dab_service_t *
dab_service_create0
  ( dab_service_t *s, const idclass_t *class, const char *uuid,
    dab_ensemble_t *mm, uint16_t sid, htsmsg_t *conf )
{
  time_t dispatch_clock = gclk();

  /* defaults for older version */
  s->s_dab_created = dispatch_clock;
  if (!conf) {
    if (sid)     s->s_components.set_service_id = sid;
  }

  if (service_create0((service_t*)s, STYPE_STD, class, uuid,
                      S_DAB, conf) == NULL)
    return NULL;

  /* Create */
  sbuf_init(&s->s_tsbuf);
  if (conf) {
    if (s->s_dab_last_seen > gclk()) /* sanity check */
      s->s_dab_last_seen = gclk();
  }
  s->s_dab_ensemble        = mm;
  LIST_INSERT_HEAD(&mm->mm_services, s, s_dab_ensemble_link);
  
  s->s_servicetype    = ST_RADIO;
  s->s_delete         = dab_service_delete;
  s->s_unref          = dab_service_unref;
  s->s_is_enabled     = dab_service_is_enabled;
  s->s_config_save    = dab_service_config_save;
  s->s_enlist         = dab_service_enlist_raw;
  s->s_start_feed     = dab_service_start;
  s->s_stop_feed      = dab_service_stop;
  s->s_refresh_feed   = dab_service_refresh;
  s->s_setsourceinfo  = dab_service_setsourceinfo;
  s->s_grace_period   = dab_service_grace_period;
  s->s_channel_name   = dab_service_channel_name;
  s->s_source         = dab_service_source;
  s->s_provider_name  = dab_service_provider_name;
  s->s_channel_icon   = dab_service_channel_icon;
  s->s_memoryinfo     = dab_service_memoryinfo;
  s->s_unseen         = dab_service_unseen;

  tvh_mutex_lock(&s->s_stream_mutex);
  service_make_nicename((service_t*)s);
  tvh_mutex_unlock(&s->s_stream_mutex);

  tvhdebug(LS_RTLSDR, "%s - add service %04X %s",
           mm->mm_nicename, service_id16(s), s->s_dab_svcname);

  /* Notification */
  idnode_notify_changed(&mm->mm_id);
  idnode_notify_changed(&mm->mm_network->mn_id);

  /* Save the create time */
  if (s->s_dab_created == dispatch_clock)
    service_request_save((service_t *)s);

  return s;
}

/*
 * Find service
 */
dab_service_t *
dab_service_find
  (dab_ensemble_t *mm, uint16_t sid, int create, int *save )
{
  dab_service_t *s;

  /* Validate */
  lock_assert(&global_lock);

  /* Find existing service */
  LIST_FOREACH(s, &mm->mm_services, s_dab_ensemble_link) {
    if (service_id16(s) == sid) {
      if (create) {
        if ((save && *save) || s->s_dab_last_seen + 3600 < gclk()) {
          s->s_dab_last_seen = gclk();
          if (save) *save = 1;
        }
      }
      return s;
    }
  }

  /* Create */
  if (create) {
    s = mm->mm_network->mn_create_service(mm, sid);
    s->s_dab_created = s->s_dab_last_seen = gclk();
    if (save) *save = 1;
  }

  return s;
}

/*
 * Raw DAB Service
 */

const idclass_t dab_service_raw_class =
{
  .ic_super      = &service_raw_class,
  .ic_class      = "dab_raw_service",
  .ic_caption    = N_("DAB raw service"),
  .ic_properties = NULL
};

static void
dab_service_raw_setsourceinfo(service_t *t, source_info_t *si)
{
  dab_service_setsourceinfo(t, si);

  free(si->si_service);
  si->si_service = strdup("Raw PID Subscription");
}

dab_service_t *
dab_service_create_raw ( dab_ensemble_t *mm )
{
  dab_service_t *s = calloc(1, sizeof(*s));

  if (service_create0((service_t*)s, STYPE_RAW,
                      &dab_service_raw_class, NULL,
                      S_DAB, NULL) == NULL) {
    free(s);
    return NULL;
  }

  sbuf_init(&s->s_tsbuf);

  s->s_dab_ensemble        = mm;

  s->s_delete         = dab_service_delete;
  s->s_is_enabled     = dab_service_is_enabled_raw;
  s->s_config_save    = dab_service_config_save;
  s->s_enlist         = dab_service_enlist_raw;
  s->s_start_feed     = dab_service_start;
  s->s_stop_feed      = dab_service_stop;
  s->s_refresh_feed   = dab_service_refresh;
  s->s_setsourceinfo  = dab_service_raw_setsourceinfo;
  s->s_grace_period   = dab_service_grace_period;
  s->s_channel_name   = dab_service_channel_name;
  s->s_source         = dab_service_source;
  s->s_provider_name  = dab_service_provider_name;
  s->s_channel_icon   = dab_service_channel_icon;
  s->s_memoryinfo     = dab_service_memoryinfo;

  tvh_mutex_lock(&s->s_stream_mutex);
  free(s->s_nicename);
  s->s_nicename = strdup(mm->mm_nicename);
  tvh_mutex_unlock(&s->s_stream_mutex);

  tvhdebug(LS_RTLSDR, "%s - add raw service", mm->mm_nicename);

  return s;
}

