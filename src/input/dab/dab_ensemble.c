/*
 *  Tvheadend - DAB Ensemble
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

#include "idnode.h"
#include "queue.h"
#include "input.h"
#include "subscriptions.h"
#include "streaming.h"
#include "channels.h"
#include "access.h"
#include "profile.h"
#include "epggrab.h"

#include <assert.h>

static void dab_ensemble_scan_timeout ( void *p );
static void dab_ensemble_do_stop ( dab_ensemble_t *mm, int delconf );

/* ****************************************************************************
 * Ensemble instance (input linkage)
 * ***************************************************************************/

const idclass_t dab_ensemble_instance_class =
{
  .ic_super      = &tvh_input_instance_class,
  .ic_class      = "dab_ensemble_instance",
  .ic_caption    = N_("DAB Ensemble PHY"),
  .ic_perm_def   = ACCESS_ADMIN
};

void
dab_ensemble_instance_delete
  ( tvh_input_instance_t *tii )
{
  dab_ensemble_instance_t *mmi = (dab_ensemble_instance_t *)tii;

  idnode_save_check(&tii->tii_id, 1);
  idnode_unlink(&tii->tii_id);
  LIST_REMOVE(mmi, mmi_ensemble_link);
  LIST_REMOVE(tii, tii_input_link);
  tvh_mutex_destroy(&mmi->tii_stats_mutex);
  free(mmi);
}

/* ****************************************************************************
 * Class definition
 * ***************************************************************************/

static int
dab_ensemble_class_frequency_set ( void *o, const void *v )
{
  dab_ensemble_t *lm = o;
  uint32_t val = *(uint32_t *)v;

  if (val < 1000)
    val *= 1000000;
  else if (val < 1000000)
    val *= 1000;

  if (val != lm->mm_freq) {
    lm->mm_freq = val;
    return 1;
  }
  return 0;
}

static htsmsg_t *
dab_ensemble_class_save ( idnode_t *self, char *filename, size_t fsize )
{
  dab_ensemble_t *mm = (dab_ensemble_t*)self;
  if (mm->mm_config_save)
    return mm->mm_config_save(mm, filename, fsize);
  return NULL;
}

static void
dab_ensemble_class_delete ( idnode_t *self )
{
  dab_ensemble_t *mm = (dab_ensemble_t*)self;
  if (mm->mm_delete) mm->mm_delete(mm, 1);
}

static void
dab_ensemble_class_get_title
  ( idnode_t *self, const char *lang, char *dst, size_t dstsize )
{
  dab_ensemble_nice_name((dab_ensemble_t*)self, dst, dstsize);
}

static const void *
dab_ensemble_class_get_num_svc ( void *ptr )
{
  static int n;
  dab_ensemble_t *mm = ptr;
  dab_service_t *s;

  n = 0;
  LIST_FOREACH(s, &mm->mm_services, s_dab_ensemble_link)
    n++;

  return &n;
}

static const void *
dab_ensemble_class_get_num_chn ( void *ptr )
{
  static int n;
  dab_ensemble_t *mm = ptr;
  dab_service_t *s;
  idnode_list_mapping_t *ilm;

  n = 0;
  LIST_FOREACH(s, &mm->mm_services, s_dab_ensemble_link)
    LIST_FOREACH(ilm, &s->s_channels, ilm_in1_link)
      n++;

  return &n;
}

static const void *
dab_ensemble_class_get_network ( void *ptr )
{
  static char buf[512], *s = buf;
  dab_ensemble_t *mm = ptr;
  if (mm && mm->mm_network && mm->mm_network->mn_display_name)
    mm->mm_network->mn_display_name(mm->mm_network, buf, sizeof(buf));
  else
    *buf = 0;
  return &s;
}

static const void *
dab_ensemble_class_get_network_uuid ( void *ptr )
{
  dab_ensemble_t *mm = ptr;
  if (mm && mm->mm_network)
    idnode_uuid_as_str(&mm->mm_network->mn_id, prop_sbuf);
  else
    prop_sbuf[0] = '\0';
  return &prop_sbuf_ptr;
}

static const void *
dab_ensemble_class_get_name ( void *ptr )
{
  static char buf[512], *s = buf;
  dab_ensemble_t *mm = ptr;
  if (mm && mm->mm_display_name)
    mm->mm_display_name(mm, buf, sizeof(buf));
  else
    *buf = 0;
  return &s;
}

static struct strtab
scan_state_tab[] = {
  { N_("IDLE"),        MM_SCAN_STATE_IDLE },
  { N_("PEND"),        MM_SCAN_STATE_PEND },
  { N_("IDLE PEND"),   MM_SCAN_STATE_IPEND },
  { N_("ACTIVE"),      MM_SCAN_STATE_ACTIVE },
};

static struct strtab
scan_result_tab[] = {
 { N_("NONE"),         MM_SCAN_NONE },
 { N_("OK"),           MM_SCAN_OK   },
 { N_("FAIL"),         MM_SCAN_FAIL },
 { N_("OK (partial)"), MM_SCAN_PARTIAL },
 { N_("IGNORE"),       MM_SCAN_IGNORE },
};

int
dab_ensemble_class_scan_state_set ( void *o, const void *p )
{
  dab_ensemble_t *mm = o;
  int state = *(int*)p;

  /* Ignore */
  if (!mm->mm_is_enabled(mm))
    return 0;
  
  /* Start */
  if (state == MM_SCAN_STATE_PEND ||
      state == MM_SCAN_STATE_IPEND ||
      state == MM_SCAN_STATE_ACTIVE) {

    /* Start (only if required) */
    dab_network_scan_queue_add(mm, SUBSCRIPTION_PRIO_SCAN_USER,
                                  SUBSCRIPTION_USERSCAN, 0);

  /* Stop */
  } else if (state == MM_SCAN_STATE_IDLE) {

    /* No change */
    if (mm->mm_scan_state == MM_SCAN_STATE_IDLE)
      return 0;

    /* Update */
    dab_network_scan_ensemble_cancel(mm, 0);

  /* Invalid */
  } else {
  }

  return 1;
}

static htsmsg_t *
dab_ensemble_class_scan_state_enum ( void *p, const char *lang )
{
  return strtab2htsmsg(scan_state_tab, 1, lang);
}

static htsmsg_t *
dab_ensemble_class_scan_result_enum ( void *p, const char *lang )
{
  return strtab2htsmsg(scan_result_tab, 1, lang);
}

static void
dab_ensemble_class_enabled_notify ( void *p, const char *lang )
{
  dab_ensemble_t *mm = p;
  if (!mm->mm_is_enabled(mm)) {
    mm->mm_stop(mm, 1, SM_CODE_MUX_NOT_ENABLED);
    dab_network_scan_ensemble_cancel(mm, 0);
    if (mm->mm_enabled == MM_IGNORE) {
      dab_ensemble_do_stop(mm, 1);
      mm->mm_scan_result = MM_SCAN_IGNORE;
    }
  }
}

static htsmsg_t *
dab_ensemble_enable_list ( void *o, const char *lang )
{
  static const struct strtab tab[] = {
    { N_("Ignore"),                   MM_IGNORE },
    { N_("Disable"),                  MM_DISABLE },
    { N_("Enable"),                   MM_ENABLE },
  };
  return strtab2htsmsg(tab, 1, lang);
}

CLASS_DOC(dab_ensemble)

const idclass_t dab_ensemble_class =
{
  .ic_class      = "dab_ensemble",
  .ic_caption    = N_("DAB Inputs - Ensemble"),
  .ic_event      = "dab_ensemble",
  .ic_doc        = tvh_doc_dab_ensemble_class,
  .ic_perm_def   = ACCESS_ADMIN,
  .ic_save       = dab_ensemble_class_save,
  .ic_delete     = dab_ensemble_class_delete,
  .ic_get_title  = dab_ensemble_class_get_title,
  .ic_properties = (const property_t[]){
    {
      .type     = PT_INT,
      .id       = "enabled",
      .name     = N_("Enabled"),
      .desc     = N_("Enable, disable or ignore the ensemble. "
                     "When the ensemble is marked as ignore, "
                     "all discovered services are removed."),
      .off      = offsetof(dab_ensemble_t, mm_enabled),
      .def.i    = MM_ENABLE,
      .list     = dab_ensemble_enable_list,
      .notify   = dab_ensemble_class_enabled_notify,
      .opts     = PO_DOC_NLIST
    },
    {
      .type     = PT_U32,
      .id       = "frequency",
      .name     = N_("Frequency (Hz)"),
      .desc     = N_("The frequency of the ensemble (in Hertz)."),
      .off      = offsetof(dab_ensemble_t, mm_freq),
      .set      = dab_ensemble_class_frequency_set,
    },
    {
      .type     = PT_STR,
      .id       = "network",
      .name     = N_("Network"),
      .desc     = N_("The network the ensemble is on."),
      .opts     = PO_RDONLY | PO_NOSAVE,
      .get      = dab_ensemble_class_get_network,
    },
    {
      .type     = PT_STR,
      .id       = "network_uuid",
      .name     = N_("Network UUID"),
      .desc     = N_("The networks' universally unique identifier (UUID)."),
      .opts     = PO_RDONLY | PO_NOSAVE | PO_HIDDEN | PO_EXPERT,
      .get      = dab_ensemble_class_get_network_uuid,
    },
    {
      .type     = PT_STR,
      .id       = "name",
      .name     = N_("Name"),
      .desc     = N_("The name (or frequency) the ensemble is on."),
      .opts     = PO_RDONLY | PO_NOSAVE,
      .get      = dab_ensemble_class_get_name,
    },
    {
      .type     = PT_STR,
      .id       = "pnetwork_name",
      .name     = N_("Provider network name"),
      .desc     = N_("The provider's network name."),
      .off      = offsetof(dab_ensemble_t, mm_provider_network_name),
      .opts     = PO_RDONLY | PO_HIDDEN | PO_EXPERT,
    },
    {
      .type     = PT_U32,
      .id       = "onid",
      .name     = N_("Original network ID"),
      .desc     = N_("The provider's network ID."),
      .opts     = PO_RDONLY | PO_ADVANCED,
      .off      = offsetof(dab_ensemble_t, mm_onid),
    },
    {
      .type     = PT_INT,
      .id       = "scan_state",
      .name     = N_("Scan status"),
      .desc     = N_("The scan state. New ensembles will automatically be "
                     "changed to the PEND state. You can change this to "
                     "ACTIVE to queue a scan of this ensemble."),
      .off      = offsetof(dab_ensemble_t, mm_scan_state),
      .set      = dab_ensemble_class_scan_state_set,
      .list     = dab_ensemble_class_scan_state_enum,
      .opts     = PO_ADVANCED | PO_NOSAVE | PO_SORTKEY | PO_DOC_NLIST,
    },
    {
      .type     = PT_INT,
      .id       = "scan_result",
      .name     = N_("Scan result"),
      .desc     = N_("The outcome of the last scan performed."),
      .off      = offsetof(dab_ensemble_t, mm_scan_result),
      .opts     = PO_RDONLY | PO_SORTKEY | PO_DOC_NLIST,
      .list     = dab_ensemble_class_scan_result_enum,
    },
    {
      .type     = PT_INT,
      .id       = "num_svc",
      .name     = N_("Services"),
      .desc     = N_("The total number of services found."),
      .opts     = PO_RDONLY | PO_NOSAVE,
      .get      = dab_ensemble_class_get_num_svc,
    },
    {
      .type     = PT_INT,
      .id       = "num_chn",
      .name     = N_("Mapped"),
      .desc     = N_("The number of services currently mapped to "
                     "channels."),
      .opts     = PO_RDONLY | PO_NOSAVE,
      .get      = dab_ensemble_class_get_num_chn,
    },
    {
      .type     = PT_TIME,
      .id       = "created",
      .name     = N_("Created"),
      .desc     = N_("When the ensemble was created."),
      .off      = offsetof(dab_ensemble_t, mm_created),
      .opts     = PO_ADVANCED | PO_RDONLY,
    },
    {
      .type     = PT_TIME,
      .id       = "scan_first",
      .name     = N_("First scan"),
      .desc     = N_("When the mux was successfully scanned for the first time."),
      .off      = offsetof(dab_ensemble_t, mm_scan_first),
      .opts     = PO_ADVANCED | PO_RDONLY,
    },
    {
      .type     = PT_TIME,
      .id       = "scan_last",
      .name     = N_("Last scan"),
      .desc     = N_("When the ensemble was successfully scanned."),
      .off      = offsetof(dab_ensemble_t, mm_scan_last_seen),
      .opts     = PO_ADVANCED | PO_RDONLY,
    },
    {}
  }
};

/* ****************************************************************************
 * Class methods
 * ***************************************************************************/

static void
dab_ensemble_display_name ( dab_ensemble_t *mm, char *buf, size_t len )
{
  snprintf(buf, len, "Ensemble [onid:%04X]", mm->mm_onid);
}

void
dab_ensemble_free ( dab_ensemble_t *mm )
{
  free(mm->mm_provider_network_name);
  free(mm->mm_nicename);
  free(mm);
}

void
dab_ensemble_delete ( dab_ensemble_t *mm, int delconf )
{
  idnode_save_check(&mm->mm_id, delconf);

  tvhinfo(LS_RTLSDR, "%s (%p) - deleting", mm->mm_nicename, mm);

  /* Stop */
  mm->mm_stop(mm, 1, SM_CODE_ABORTED);

  /* Remove from network */
  LIST_REMOVE(mm, mm_network_link);

  /* Real stop */
  dab_ensemble_do_stop(mm, delconf);

  /* Free memory */
  idnode_save_check(&mm->mm_id, 1);
  idnode_unlink(&mm->mm_id);
  dab_ensemble_release(mm);
}

static htsmsg_t *
dab_ensemble_config_save ( dab_ensemble_t *mm, char *filename, size_t fsize )
{
  char ubuf1[UUID_HEX_SIZE];
  char ubuf2[UUID_HEX_SIZE];
  htsmsg_t *c = htsmsg_create_map();
  if (filename == NULL) {
    dab_ensemble_save(mm, c, 1);
  } else {
    dab_ensemble_save(mm, c, 0);
    snprintf(filename, fsize, "input/dab/networks/%s/ensembles/%s",
             idnode_uuid_as_str(&mm->mm_network->mn_id, ubuf1),
             idnode_uuid_as_str(&mm->mm_id, ubuf2));
  }
  return c;
}

static int
dab_ensemble_is_enabled ( dab_ensemble_t *mm )
{
  return mm->mm_network->mn_enabled && mm->mm_enabled == MM_ENABLE;
}

static void
dab_ensemble_create_instances ( dab_ensemble_t *mm )
{
  dab_network_link_t *mnl;
  LIST_FOREACH(mnl, &mm->mm_network->mn_inputs, mnl_mn_link) {
    dab_input_t *mi = mnl->mnl_input;
    if (mi->mi_is_enabled(mi, mm, 0, -1) != MI_IS_ENABLED_NEVER)
      mi->mi_create_ensemble_instance(mi, mm);
  }
}

static int
dab_ensemble_has_subscribers ( dab_ensemble_t *mm, const char *name )
{
  dab_ensemble_instance_t *mmi = mm->mm_active;
  if (mmi) {
    if (mmi->mmi_input->mi_has_subscription(mmi->mmi_input, mm)) {
      tvhtrace(LS_MPEGTS, "%s - keeping mux", name);
      return 1;
    }
  }
  return 0;
}

static void
dab_ensemble_stop ( dab_ensemble_t *mm, int force, int reason )
{
  char *s;
  dab_ensemble_instance_t *mmi = mm->mm_active, *mmi2;
  dab_input_t *mi = NULL, *mi2;

  if (!force && dab_ensemble_has_subscribers(mm, mm->mm_nicename))
    return;

  /* Stop possible recursion */
  if (!mmi) return;

  tvhdebug(LS_RTLSDR, "%s - stopping mux%s", mm->mm_nicename, force ? " (forced)" : "");

  mi = mmi->mmi_input;
  assert(mi);

  /* Linked input */
  if (mi->mi_linked) {
    mi2 = dab_input_find(mi->mi_linked);
    if (mi2 && (mmi2 = LIST_FIRST(&mi2->mi_ensemble_active)) != NULL) {
      if (mmi2 && !dab_ensemble_has_subscribers(mmi2->mmi_ensemble, mmi2->mmi_ensemble->mm_nicename)) {
        s = mi2->mi_linked;
        mi2->mi_linked = NULL;
        dab_ensemble_unsubscribe_linked(mi2, NULL);
        mi2->mi_linked = s;
      } else {
        if (!force) {
          tvhtrace(LS_RTLSDR, "%s - keeping subscribed (linked tuner active)", mm->mm_nicename);
          return;
        }
      }
    }
  }

  if (mm->mm_active != mmi)
    return;

  mi->mi_stopping_ensemble(mi, mmi);
  mi->mi_stop_ensemble(mi, mmi);
  mi->mi_stopped_ensemble(mi, mmi);

  assert(mm->mm_active == NULL);

  tvhtrace(LS_RTLSDR, "%s - mi=%p", mm->mm_nicename, (void *)mi);

  /* Scanning */
  dab_network_scan_ensemble_cancel(mm, 1);

  /* Events */
  dab_fire_event1(mm, ml_ensemble_stop, reason);
}

static void
dab_ensemble_do_stop ( dab_ensemble_t *mm, int delconf )
{
  dab_ensemble_instance_t *mmi;
  th_subscription_t *ths;
  dab_service_t *s;

  /* Cancel scan */
  dab_network_scan_queue_del(mm);

  /* Remove instances */
  while ((mmi = LIST_FIRST(&mm->mm_instances))) {
    mmi->tii_delete((tvh_input_instance_t *)mmi);
  }

  /* Remove raw subscribers */
  while ((ths = LIST_FIRST(&mm->mm_raw_subs))) {
    subscription_unsubscribe(ths, 0);
  }

  /* Delete services */
  while ((s = LIST_FIRST(&mm->mm_services))) {
    service_destroy((service_t*)s, delconf);
  }
}

static void
dab_ensemble_scan_active
  ( dab_ensemble_t *mm, const char *buf, dab_input_t *mi )
{
  int t;

  /* Setup scan */
  if (mm->mm_scan_state == MM_SCAN_STATE_PEND ||
      mm->mm_scan_state == MM_SCAN_STATE_IPEND) {
    dab_network_scan_ensemble_active(mm);

    /* Get timeout */
    t = dab_input_grace(mi, mm);
  
    /* Setup timeout */
    mtimer_arm_rel(&mm->mm_scan_timeout, dab_ensemble_scan_timeout, mm, sec2mono(t));
  }
}

static int
dab_ensemble_keep_exists
  ( dab_input_t *mi )
{
  const dab_ensemble_instance_t *mmi;
  const service_t *s;
  const th_subscription_t *ths;
  int ret;

  lock_assert(&global_lock);

  if (!mi)
    return 0;

  ret = 0;
  LIST_FOREACH(mmi, &mi->mi_ensemble_active, mmi_active_link)
    LIST_FOREACH(ths, &mmi->mmi_ensemble->mm_raw_subs, ths_ensemble_link) {
      s = ths->ths_service;
      if (s && s->s_type == STYPE_RAW && s->s_source_type == S_DAB && !strcmp(ths->ths_title, "keep")) {
        ret = 1;
        break;
      }
    }
  return ret;
}

static int
dab_ensemble_subscribe_keep
  ( dab_ensemble_t *mm, dab_input_t *mi )
{
  char *s;
  int r;

  s = mi->mi_linked;
  mi->mi_linked = NULL;
  tvhtrace(LS_RTLSDR, "subscribe keep for '%s' (%p)", mi->mi_name, mm);
  r = dab_ensemble_subscribe(mm, mi, "keep", SUBSCRIPTION_PRIO_KEEP,
                           SUBSCRIPTION_ONESHOT | SUBSCRIPTION_MINIMAL);
  mi->mi_linked = s;
  return r;
}

static void
dab_ensemble_subscribe_linked
  ( dab_input_t *mi, dab_ensemble_t *mm )
{
  dab_input_t *mi2 = dab_input_find(mi->mi_linked);
  dab_ensemble_instance_t *mmi2;
  dab_network_link_t *mnl2;
  dab_ensemble_t *mm2;
  char buf1[128], buf2[128];
  const char *serr = "All";
  int r = 0;

  tvhtrace(LS_RTLSDR, "subscribe linked");

  if (!dab_ensemble_keep_exists(mi) && (r = dab_ensemble_subscribe_keep(mm, mi))) {
    serr = "active1";
    goto fatal;
  }

  if (!mi2)
    return;

  if (dab_ensemble_keep_exists(mi2))
    return;

  mmi2 = LIST_FIRST(&mi2->mi_ensemble_active);
  if (mmi2) {
    if (!dab_ensemble_subscribe_keep(mmi2->mmi_ensemble, mi2))
      return;
    serr = "active2";
    goto fatal;
  }

  /* Try ensembles within same network */
  LIST_FOREACH(mnl2, &mi2->mi_networks, mnl_mi_link)
    if (mnl2->mnl_network == mm->mm_network)
      LIST_FOREACH(mm2, &mnl2->mnl_network->mn_ensembles, mm_network_link)
        if (!mm2->mm_active && MM_SCAN_CHECK_OK(mm) &&
            !LIST_EMPTY(&mm2->mm_services))
          if (!dab_ensemble_subscribe_keep(mm2, mi2))
            return;

  /* Try all other ensembles */
  LIST_FOREACH(mnl2, &mi2->mi_networks, mnl_mi_link)
    if (mnl2->mnl_network != mm->mm_network)
      LIST_FOREACH(mm2, &mnl2->mnl_network->mn_ensembles, mm_network_link)
        if (!mm2->mm_active && MM_SCAN_CHECK_OK(mm) &&
            !LIST_EMPTY(&mm2->mm_services))
          if (!dab_ensemble_subscribe_keep(mm2, mi2))
            return;

fatal:
  mi ->mi_display_name(mi,  buf1, sizeof(buf1));
  mi2->mi_display_name(mi2, buf2, sizeof(buf2));
  tvherror(LS_RTLSDR, "%s - %s - linked input cannot be started (%s: %i)", buf1, buf2, serr, r);
}

void
dab_ensemble_unsubscribe_linked
  ( dab_input_t *mi, service_t *t )
{
  th_subscription_t *ths, *ths_next;

  if (mi) {
    tvhtrace(LS_RTLSDR, "unsubscribing linked");

    for (ths = LIST_FIRST(&subscriptions); ths; ths = ths_next) {
      ths_next = LIST_NEXT(ths, ths_global_link);
      if (ths->ths_source == (tvh_input_t *)mi && !strcmp(ths->ths_title, "keep") &&
          ths->ths_service != t)
        subscription_unsubscribe(ths, UNSUBSCRIBE_FINAL);
    }
  }
}

int
dab_ensemble_instance_start
  ( dab_ensemble_instance_t **mmiptr, service_t *t, int weight )
{
  int r;
  char buf[256];
  dab_ensemble_instance_t *mmi = *mmiptr;
  dab_ensemble_t          * mm = mmi->mmi_ensemble;
  dab_input_t        * mi = mmi->mmi_input;
  dab_service_t      *  s;

  /* Already active */
  if (mm->mm_active) {
    *mmiptr = mm->mm_active;
    tvhdebug(LS_RTLSDR, "%s - already active", mm->mm_nicename);
    dab_ensemble_scan_active(mm, buf, (*mmiptr)->mmi_input);
    return 0;
  }

  /* Update nicename */
  dab_ensemble_update_nice_name(mm);

  /* Dead service check */
  LIST_FOREACH(s, &mm->mm_services, s_dab_ensemble_link)
    s->s_dab_check_seen = s->s_dab_last_seen;

  /* Start */
  mi->mi_display_name(mi, buf, sizeof(buf));
  tvhinfo(LS_RTLSDR, "%s - tuning on %s", mm->mm_nicename, buf);

  if (mi->mi_linked)
    dab_ensemble_unsubscribe_linked(mi, t);

  r = mi->mi_warm_ensemble(mi, mmi);
  if (r) return r;
  r = mi->mi_start_ensemble(mi, mmi, weight);
  if (r) return r;

  /* Start */
  tvhdebug(LS_RTLSDR, "%s - started", mm->mm_nicename);
  mm->mm_start_monoclock = mclk();
  mi->mi_started_ensemble(mi, mmi);

  /* Event handler */
  dab_fire_event(mm, ml_ensemble_start);

  /* Link */
  dab_ensemble_scan_active(mm, buf, mi);

  if (mi->mi_linked)
    dab_ensemble_subscribe_linked(mi, mm);

  return 0;
}

dab_ensemble_instance_t *
dab_ensemble_instance_create0
  ( dab_ensemble_instance_t *mmi, const idclass_t *class, const char *uuid, 
    dab_input_t *mi, dab_ensemble_t *mm )
{
  // TODO: does this need to be an idnode?
  if (idnode_insert(&mmi->tii_id, uuid, &dab_ensemble_instance_class, 0)) {
    free(mmi);
    return NULL;
  }

  tvh_mutex_init(&mmi->tii_stats_mutex, NULL);

  /* Setup links */
  mmi->mmi_ensemble   = mm;
  mmi->mmi_input = mi;
  
  /* Callbacks */
  mmi->tii_delete = dab_ensemble_instance_delete;
  mmi->tii_clear_stats = tvh_input_instance_clear_stats;

  LIST_INSERT_HEAD(&mm->mm_instances, mmi, mmi_ensemble_link);
  LIST_INSERT_HEAD(&mi->mi_ensemble_instances, (tvh_input_instance_t *)mmi, tii_input_link);

  return mmi;
}

/* **************************************************************************
 * Scanning
 * *************************************************************************/

static void
dab_ensemble_scan_service_check ( dab_ensemble_t *mm )
{
  dab_service_t *s, *snext;
  time_t last_seen;

  /*
   * Disable "not seen" services. It's quite easy algorithm which
   * compares the time for the most recent services with others.
   * If there is a big gap (24 hours) the service will be removed.
   *
   * Note that this code is run only when the PAT table scan is
   * fully completed (all live services are known at this point).
   */
  last_seen = 0;
  LIST_FOREACH(s, &mm->mm_services, s_dab_ensemble_link)
    if (last_seen < s->s_dab_check_seen)
      last_seen = s->s_dab_check_seen;
  for (s = LIST_FIRST(&mm->mm_services); s; s = snext) {
    snext = LIST_NEXT(s, s_dab_ensemble_link);
    if (s->s_enabled && s->s_auto != SERVICE_AUTO_OFF &&
        s->s_dab_check_seen + 24 * 3600 < last_seen) {
      tvhinfo(LS_RTLSDR, "disabling service %s [sid %04X/%d] (missing in PAT/SDT)",
              s->s_nicename ?: "<unknown>",
              service_id16(s), service_id16(s));
      service_set_enabled((service_t *)s, 0, SERVICE_AUTO_PAT_MISSING);
    }
  }
}

void
dab_ensemble_scan_done ( dab_ensemble_t *mm, const char *buf, int res )
{
  int complete = 0;
  int incomplete = 0;
  dab_service_t *ms;

  assert(mm->mm_scan_state == MM_SCAN_STATE_ACTIVE);

  /* Log */
  tvh_mutex_lock(&mm->mm_tables_lock);
  LIST_FOREACH(ms, &mm->mm_services, s_dab_ensemble_link)
    if (ms->s_dab_svcname && ms->subChId) {
      complete++;
    } else {
      incomplete++;
    }
  
  tvh_mutex_unlock(&mm->mm_tables_lock);

  /* override if all tables were found */
  if (res < 0 && incomplete <= 0 && complete > 2)
    res = 1;

  if (res < 0) {
    /* is threshold 3 missing tables enough? */
    if (incomplete > 0 && complete > 0 && incomplete <= 3) {
      tvhinfo(LS_RTLSDR, "%s - scan complete (partial - %d/%d tables)", buf, complete, incomplete);
      dab_network_scan_ensemble_partial(mm);
    } else {
      tvhwarn(LS_RTLSDR, "%s - scan timed out (%d/%d tables)", buf, complete, incomplete);
      dab_network_scan_ensemble_fail(mm);
    }
  } else if (res) {
    tvhinfo(LS_RTLSDR, "%s scan complete", buf);
    dab_network_scan_ensemble_done(mm);
    dab_ensemble_scan_service_check(mm);
  } else {
    tvhinfo(LS_RTLSDR, "%s - scan no data, failed", buf);
    dab_network_scan_ensemble_fail(mm);
  }
}

static void
dab_ensemble_scan_timeout ( void *aux )
{
  int complete = 0;
  int incomplete = 0;
  dab_service_t *ms;
  dab_ensemble_t *mm = aux;

  /* Timeout */
  if (mm->mm_scan_init) {
    dab_ensemble_scan_done(mm, mm->mm_nicename, -1);
    return;
  }
  mm->mm_scan_init = 1;
  
  /* Check tables */
  tvh_mutex_lock(&mm->mm_tables_lock);
  LIST_FOREACH(ms, &mm->mm_services, s_dab_ensemble_link)
    if (ms->s_dab_svcname && ms->subChId) {
      complete++;
    } else {
      incomplete++;
    }
  tvh_mutex_unlock(&mm->mm_tables_lock);
      
  /* No DATA - give up now */
  if ((complete + incomplete) == 0) {
    dab_ensemble_scan_done(mm, mm->mm_nicename, 0);

  /* Pending tables (another 20s or 30s - bit arbitrary) */
  } else if (incomplete > 0) {
    tvhtrace(LS_RTLSDR, "%s - scan needs more time", mm->mm_nicename);
    mtimer_arm_rel(&mm->mm_scan_timeout, dab_ensemble_scan_timeout, mm, sec2mono(30));
    return;

  /* Complete */
  } else {
    dab_ensemble_scan_done(mm, mm->mm_nicename, 1);
  }
}

/* **************************************************************************
 * Creation / Config
 * *************************************************************************/

dab_ensemble_t *dab_ensemble_create0
  ( dab_ensemble_t *mm, const idclass_t *class, const char *uuid,
    dab_network_t *mn, uint32_t onid, htsmsg_t *conf )
{
  htsmsg_t *c, *c2, *e;
  htsmsg_field_t *f;
  char ubuf1[UUID_HEX_SIZE];
  char ubuf2[UUID_HEX_SIZE];
  int i;

  if (idnode_insert(&mm->mm_id, uuid, class, 0)) {
    if (uuid)
      tvherror(LS_RTLSDR, "invalid mux uuid '%s'", uuid);
    free(mm);
    return NULL;
  }

  mm->mm_refcount            = 1;

  /* Enabled by default */
  mm->mm_enabled             = MM_ENABLE;

  /* Identification */
  mm->mm_onid                = onid;

  /* Add to network */
  LIST_INSERT_HEAD(&mn->mn_ensembles, mm, mm_network_link);
  mm->mm_network             = mn;

  /* Debug/Config */
  mm->mm_delete              = dab_ensemble_delete;
  mm->mm_free                = dab_ensemble_free;
  mm->mm_display_name        = dab_ensemble_display_name;
  mm->mm_config_save         = dab_ensemble_config_save;
  mm->mm_is_enabled          = dab_ensemble_is_enabled;

  /* Start/stop */
  mm->mm_stop                = dab_ensemble_stop;
  mm->mm_create_instances    = dab_ensemble_create_instances;

  /* Table processing */
  tvh_mutex_init(&mm->mm_tables_lock, NULL);

  mm->mm_created             = gclk();

  /* Configuration */
  if (conf)
    idnode_load(&mm->mm_id, conf);
    
  dab_ensemble_update_nice_name(mm);

  /* No config */
  if (!conf) return mm;

  c = htsmsg_get_map(conf, "serviceComponents");
  if (c) {
    HTSMSG_FOREACH(f, c) {
      if (!(e = htsmsg_get_map_by_field(f))) continue;
      i = atoi(htsmsg_field_name(f));
      mm->ServiceComps [i].inUse = 1;
      htsmsg_get_s32(e, "TMid", &mm->ServiceComps [i].TMid);
      htsmsg_get_s32(e, "componentNr", &mm->ServiceComps [i].componentNr);
      htsmsg_get_s32(e, "ASCTy", &mm->ServiceComps [i].ASCTy);
      htsmsg_get_s32(e, "PS_flag", &mm->ServiceComps [i].PS_flag);
      htsmsg_get_s32(e, "subchannelId", &mm->ServiceComps [i].subchannelId);
      
    }
  }

  c = htsmsg_get_map(conf, "subchannels");
  if (c) {
    HTSMSG_FOREACH(f, c) {
      if (!(e = htsmsg_get_map_by_field(f))) continue;
      i = atoi(htsmsg_field_name(f));
      mm->subChannels [i].inUse = 1;
      htsmsg_get_s32(e, "SubChId", &mm->subChannels [i].SubChId);
      htsmsg_get_s32(e, "StartAddr", &mm->subChannels [i].StartAddr);
      htsmsg_get_s32(e, "Length", &mm->subChannels [i].Length);
      htsmsg_get_s32(e, "shortForm", &mm->subChannels [i].shortForm);
      htsmsg_get_s32(e, "protLevel", &mm->subChannels [i].protLevel);
      htsmsg_get_s32(e, "BitRate", &mm->subChannels [i].BitRate);
      htsmsg_get_s32(e, "language", &mm->subChannels [i].language);
      htsmsg_get_s32(e, "FEC_scheme", &mm->subChannels [i].FEC_scheme);
    }
  }

  /* Services */
  c2 = NULL;
  c = htsmsg_get_map(conf, "services");
  if (c == NULL)
    c = c2 = hts_settings_load_r(1, "input/dab/networks/%s/ensembles/%s/services",
                                 idnode_uuid_as_str(&mn->mn_id, ubuf1),
                                 idnode_uuid_as_str(&mm->mm_id, ubuf2));

  if (c) {
    HTSMSG_FOREACH(f, c) {
      if (!(e = htsmsg_get_map_by_field(f))) continue;
      dab_service_create1(htsmsg_field_name(f), mm, 0, e);
    }
    htsmsg_destroy(c2);
  }

  return mm;
}

dab_ensemble_t *
dab_ensemble_post_create ( dab_ensemble_t *mm )
{
  dab_network_t *mn;

  if (mm == NULL)
    return NULL;

  mn = mm->mm_network;
  if (mm->mm_enabled == MM_IGNORE)
    mm->mm_scan_result = MM_SCAN_IGNORE;

  dab_ensemble_update_nice_name(mm);
  tvhtrace(LS_RTLSDR, "%s - created", mm->mm_nicename);

  /* Initial scan */
  if (mm->mm_scan_result == MM_SCAN_NONE || !mn->mn_skipinitscan)
    dab_network_scan_queue_add(mm, SUBSCRIPTION_PRIO_SCAN_INIT,
                                  SUBSCRIPTION_INITSCAN, 10);
  else if (mm->mm_network->mn_idlescan)
    dab_network_scan_queue_add(mm, SUBSCRIPTION_PRIO_SCAN_IDLE,
                                  SUBSCRIPTION_IDLESCAN, 10);

  return mm;
}

void
dab_ensemble_save ( dab_ensemble_t *mm, htsmsg_t *c, int refs )
{
  dab_service_t *ms;
  htsmsg_t *root = !refs ? htsmsg_create_map() : c;
  htsmsg_t *services = !refs ? htsmsg_create_map() : htsmsg_create_list();
  htsmsg_t *subchannels = htsmsg_create_map();
  htsmsg_t *serviceComponents = htsmsg_create_map();
  htsmsg_t *e;
  char ubuf[UUID_HEX_SIZE];
  int i;

  idnode_save(&mm->mm_id, root);
  LIST_FOREACH(ms, &mm->mm_services, s_dab_ensemble_link) {
    if (refs) {
      htsmsg_add_uuid(services, NULL, &ms->s_id.in_uuid);
    } else {
      e = htsmsg_create_map();
      service_save((service_t *)ms, e);
      htsmsg_add_msg(services, idnode_uuid_as_str(&ms->s_id, ubuf), e);
    }
  }
  htsmsg_add_msg(root, "services", services);
  for (i = 0; i < 64; i ++) {
    if (!mm->ServiceComps [i]. inUse)
      continue;
    e = htsmsg_create_map();
    htsmsg_add_u32(e, "TMid", mm->ServiceComps [i].TMid);
    htsmsg_add_u32(e, "componentNr", mm->ServiceComps [i].componentNr);
    htsmsg_add_u32(e, "ASCTy", mm->ServiceComps [i].ASCTy);
    htsmsg_add_u32(e, "PS_flag", mm->ServiceComps [i].PS_flag);
    htsmsg_add_u32(e, "subchannelId", mm->ServiceComps [i].subchannelId);
    snprintf(ubuf, UUID_HEX_SIZE, "%d", i);
    htsmsg_add_msg(serviceComponents, ubuf, e);
  }
  htsmsg_add_msg(root, "serviceComponents", serviceComponents);
  for (i = 0; i < 64; i ++) {
    if (!mm->subChannels [i]. inUse)
      continue;
    e = htsmsg_create_map();
    htsmsg_add_u32(e, "SubChId", mm->subChannels [i].SubChId);
    htsmsg_add_u32(e, "StartAddr", mm->subChannels [i].StartAddr);
    htsmsg_add_u32(e, "Length", mm->subChannels [i].Length);
    htsmsg_add_u32(e, "shortForm", mm->subChannels [i].shortForm);
    htsmsg_add_u32(e, "protLevel", mm->subChannels [i].protLevel);
    htsmsg_add_u32(e, "BitRate", mm->subChannels [i].BitRate);
    htsmsg_add_u32(e, "language", mm->subChannels [i].language);
    htsmsg_add_u32(e, "FEC_scheme", mm->subChannels [i].FEC_scheme);
    snprintf(ubuf, UUID_HEX_SIZE, "%d", i);
    htsmsg_add_msg(subchannels, ubuf, e);
  }
  htsmsg_add_msg(root, "subchannels", subchannels);
  if (!refs)
    htsmsg_add_msg(c, "config", root);
}

int
dab_ensemble_set_network_name ( dab_ensemble_t *mm, const char *name )
{
  if (strcmp(mm->mm_provider_network_name ?: "", name ?: "")) {
    tvh_str_update(&mm->mm_provider_network_name, name ?: "");
    return 1;
  }
  return 0;
}

int
dab_ensemble_set_onid ( dab_ensemble_t *mm, uint32_t onid )
{
  if (onid == mm->mm_onid)
    return 0;
  mm->mm_onid = onid;
  tvhtrace(LS_RTLSDR, "%s - set onid %04X (%d)", mm->mm_nicename, onid, onid);
  idnode_changed(&mm->mm_id);
  return 1;
}

void
dab_ensemble_nice_name( dab_ensemble_t *mm, char *buf, size_t len )
{
  size_t len2;

  if (len == 0 || buf == NULL || mm == NULL) {
    if (buf && len > 0)
      *buf = '\0';
    return;
  }
  if (mm->mm_display_name)
    mm->mm_display_name(mm, buf, len);
  else
    *buf = '\0';
  len2 = strlen(buf);
  buf += len2;
  len -= len2;
  if (len2 + 16 >= len)
    return;
  strcpy(buf, " in ");
  buf += 4;
  len -= 4;
  if (mm->mm_network && mm->mm_network->mn_display_name)
    mm->mm_network->mn_display_name(mm->mm_network, buf, len);
}

void
dab_ensemble_update_nice_name( dab_ensemble_t *mm )
{
  char buf[256];

  dab_ensemble_nice_name(mm, buf, sizeof(buf));
  free(mm->mm_nicename);
  mm->mm_nicename = strdup(buf);
}

/* **************************************************************************
 * Subscriptions
 * *************************************************************************/

int
dab_ensemble_subscribe
  ( dab_ensemble_t *mm, dab_input_t *mi,
    const char *name, int weight, int flags )
{
  profile_chain_t prch;
  th_subscription_t *s;
  int err = 0;
  memset(&prch, 0, sizeof(prch));
  prch.prch_id = mm;
  s = subscription_create_from_ensemble(&prch, (tvh_input_t *)mi,
                                   weight, name,
                                   SUBSCRIPTION_NONE | flags,
                                   NULL, NULL, NULL, &err);
  return s ? 0 : (err ? err : SM_CODE_UNDEFINED_ERROR);
}

void
dab_ensemble_unsubscribe_by_name
  ( dab_ensemble_t *mm, const char *name )
{
  const service_t *t;
  th_subscription_t *s, *n;

  s = LIST_FIRST(&mm->mm_raw_subs);
  while (s) {
    n = LIST_NEXT(s, ths_mux_link);
    t = s->ths_service;
    if (t && t->s_type == STYPE_RAW && t->s_source_type == S_DAB && !strcmp(s->ths_title, name))
      subscription_unsubscribe(s, UNSUBSCRIBE_FINAL);
    s = n;
  }
}

/* **************************************************************************
 * Search
 * *************************************************************************/

dab_service_t *
dab_ensemble_find_service ( dab_ensemble_t *mm, uint16_t sid )
{
  dab_service_t *ms;
  LIST_FOREACH(ms, &mm->mm_services, s_dab_ensemble_link)
    if (service_id16(ms) == sid && ms->s_enabled)
      break;
  return ms;
}

/* **************************************************************************
 * Misc
 * *************************************************************************/

int
dab_ensemble_compare ( dab_ensemble_t *a, dab_ensemble_t *b )
{
  int r = uuid_cmp(&a->mm_network->mn_id.in_uuid,
                   &b->mm_network->mn_id.in_uuid);
  if (r)
    return r;
  return r ?: uuid_cmp(&a->mm_id.in_uuid, &b->mm_id.in_uuid);
}

