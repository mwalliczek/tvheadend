/*
 *  Tvheadend - DAB input source
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
#include "subscriptions.h"
#include "channels.h"
#include "access.h"
#include "bouquet.h"

#include <assert.h>

/* ****************************************************************************
 * Class definition
 * ***************************************************************************/

static void
dab_network_class_notify_enabled ( void *obj, const char *lang )
{
  dab_network_t *mn = (dab_network_t*)obj;
  dab_ensemble_instance_t *mmi;
  dab_ensemble_t *mm;
  if (!mn->mn_enabled) {
    LIST_FOREACH(mm, &mn->mn_ensembles, mm_network_link) {
      mmi = mm->mm_active;
      if (!mmi) continue;
      assert(mm == mmi->mmi_ensemble);
      mm->mm_stop(mm, 1, SM_CODE_ABORTED);
    }
  }
}

static htsmsg_t *
dab_network_class_save
  ( idnode_t *in, char *filename, size_t fsize )
{
  dab_network_t *mn = (dab_network_t*)in;
  if (mn->mn_config_save)
    return mn->mn_config_save(mn, filename, fsize);
  return NULL;
}

static void
dab_network_class_get_title
  ( idnode_t *in, const char *lang, char *dst, size_t dstsize )
{
  dab_network_t *mn = (dab_network_t*)in;
  *dst = 0;
  if (mn->mn_display_name)
    mn->mn_display_name(mn, dst, dstsize);
}

static const void *
dab_network_class_get_num_ensembles ( void *ptr )
{
  static int n;
  dab_ensemble_t *mm;
  dab_network_t *mn = ptr;

  n = 0;
  LIST_FOREACH(mm, &mn->mn_ensembles, mm_network_link)
    n++;

  return &n;
}

static const void *
dab_network_class_get_num_svc ( void *ptr )
{
  static int n;
  dab_ensemble_t *mm;
  dab_service_t *s;
  dab_network_t *mn = ptr;

  n = 0;
  LIST_FOREACH(mm, &mn->mn_ensembles, mm_network_link)
    LIST_FOREACH(s, &mm->mm_services, s_dab_ensemble_link)
      n++;

  return &n;
}

static const void *
dab_network_class_get_num_chn ( void *ptr )
{
  static int n;
  dab_ensemble_t *mm;
  dab_service_t *s;
  dab_network_t *mn = ptr;
  idnode_list_mapping_t *ilm;

  n = 0;
  LIST_FOREACH(mm, &mn->mn_ensembles, mm_network_link)
    LIST_FOREACH(s, &mm->mm_services, s_dab_ensemble_link)
      LIST_FOREACH(ilm, &s->s_channels, ilm_in1_link)
        n++;

  return &n;
}

static const void *
dab_network_class_get_scanq_length ( void *ptr )
{
  static int n;
  dab_ensemble_t *mm;
  dab_network_t *mn = ptr;

  n = 0;
  LIST_FOREACH(mm, &mn->mn_ensembles, mm_network_link)
    if (mm->mm_scan_state != MM_SCAN_STATE_IDLE)
      n++;

  return &n;
}

static void
dab_network_class_idlescan_notify ( void *p, const char *lang )
{
  dab_network_t *mn = p;
  dab_ensemble_t *mm;
  LIST_FOREACH(mm, &mn->mn_ensembles, mm_network_link) {
    if (mn->mn_idlescan)
      dab_network_scan_queue_add(mm, SUBSCRIPTION_PRIO_SCAN_IDLE,
                                    SUBSCRIPTION_IDLESCAN, 0);
    else if (mm->mm_scan_state  == MM_SCAN_STATE_PEND &&
             mm->mm_scan_weight == SUBSCRIPTION_PRIO_SCAN_IDLE) {
      mm->mm_scan_flags = 0;
      dab_network_scan_queue_del(mm);
    }
  }
}

static htsmsg_t *
dab_network_discovery_enum ( void *o, const char *lang )
{
  static const struct strtab tab[] = {
    { N_("Disable"),                  MN_DISCOVERY_DISABLE },
    { N_("New muxes only"),           MN_DISCOVERY_NEW },
    { N_("New muxes + changed muxes"), MN_DISCOVERY_CHANGE },
  };
  return strtab2htsmsg(tab, 1, lang);
}

CLASS_DOC(dab_network)
PROP_DOC(network_discovery)

const idclass_t dab_network_class =
{
  .ic_class      = "dab_network",
  .ic_caption    = N_("DAB Network 2"),
  .ic_doc        = tvh_doc_dab_network_class,
  .ic_event      = "dab_network",
  .ic_perm_def   = ACCESS_ADMIN,
  .ic_save       = dab_network_class_save,
  .ic_get_title  = dab_network_class_get_title,
  .ic_properties = (const property_t[]){
    {
      .type     = PT_BOOL,
      .id       = "enabled",
      .name     = N_("Enabled"),
      .desc     = N_("Enable/Disable network."),
      .off      = offsetof(dab_network_t, mn_enabled),
      .notify   = dab_network_class_notify_enabled,
    },
    {
      .type     = PT_STR,
      .id       = "networkname",
      .name     = N_("Network name"),
      .desc     = N_("Name of the network."),
      .off      = offsetof(dab_network_t, mn_network_name),
      .notify   = idnode_notify_title_changed_lang,
    },
    {
      .type     = PT_INT,
      .id       = "autodiscovery",
      .name     = N_("Network discovery"),
      .desc     = N_("Discover more muxes using the Network "
                     "Information Table (if available)."),
      .doc      = prop_doc_network_discovery,
      .off      = offsetof(dab_network_t, mn_autodiscovery),
      .list     = dab_network_discovery_enum,
      .opts     = PO_ADVANCED | PO_DOC_NLIST,
      .def.i    = MN_DISCOVERY_NEW
    },
    {
      .type     = PT_BOOL,
      .id       = "skipinitscan",
      .name     = N_("Skip startup scan"),
      .desc     = N_("Skip scanning known muxes when Tvheadend starts. "
                     "If \"startup scan\" is allowed and new muxes are "
                     "found then they will still be scanned. See Help for "
                     "more details."),
      .off      = offsetof(dab_network_t, mn_skipinitscan),
      .opts     = PO_EXPERT,
      .def.i    = 1
    },
    {
      .type     = PT_BOOL,
      .id       = "idlescan",
      .name     = N_("Idle scan muxes"),
      .desc     = N_("When nothing else is happening Tvheadend will "
                     "continuously rotate among all muxes and tune to "
                     "them to verify that they are still working when "
                     "the inputs are not used for streaming. If your "
                     "adapters have problems with lots of (endless) "
                     "tuning, disable this. Note that this option "
                     "should be OFF for the normal operation. This type "
                     "of mux probing is not required and it may cause "
                     "issues for SAT>IP (limited number of PID filters)."),
      .off      = offsetof(dab_network_t, mn_idlescan),
      .def.i    = 0,
      .notify   = dab_network_class_idlescan_notify,
      .opts     = PO_EXPERT | PO_HIDDEN,
    },
    {
      .type     = PT_INT,
      .id       = "localtime",
      .name     = N_("EIT time offset"),
      .desc     = N_("Select the time offset for EIT events."),
      .off      = offsetof(dab_network_t, mn_localtime),
      .list     = dvb_timezone_enum,
      .opts     = PO_EXPERT | PO_DOC_NLIST,
    },
    {
      .type     = PT_INT,
      .id       = "num_mux",
      .name     = N_("# Muxes"),
      .desc     = N_("Total number of ensembles found on this network."),
      .opts     = PO_RDONLY | PO_NOSAVE,
      .get      = dab_network_class_get_num_ensembles,
    },
    {
      .type     = PT_INT,
      .id       = "num_svc",
      .name     = N_("# Services"),
      .desc     = N_("Total number of services found on this network."),
      .opts     = PO_RDONLY | PO_NOSAVE,
      .get      = dab_network_class_get_num_svc,
    },
    {
      .type     = PT_INT,
      .id       = "num_chn",
      .name     = N_("# Mapped channels"),
      .desc     = N_("Total number of mapped channels on this network."),
      .opts     = PO_RDONLY | PO_NOSAVE,
      .get      = dab_network_class_get_num_chn,
    },
    {
      .type     = PT_INT,
      .id       = "scanq_length",
      .name     = N_("Scan queue length"),
      .desc     = N_("The number of muxes left to scan on this network."),
      .opts     = PO_RDONLY | PO_NOSAVE,
      .get      = dab_network_class_get_scanq_length,
    },
    {
       .type     = PT_BOOL,
       .id       = "wizard",
       .name     = N_("Wizard"),
       .off      = offsetof(dab_network_t, mn_wizard),
       .opts     = PO_NOUI
    },
    {}
  }
};

/* ****************************************************************************
 * Class methods
 * ***************************************************************************/

static void
dab_network_display_name
  ( dab_network_t *mn, char *buf, size_t len )
{
  strlcpy(buf, tvh_str_default(mn->mn_network_name, "unknown"), len);
}

static htsmsg_t *
dab_network_config_save
  ( dab_network_t *mn, char *filename, size_t size )
{
  htsmsg_t *c = htsmsg_create_map();
  char ubuf[UUID_HEX_SIZE];
  idnode_save(&mn->mn_id, c);
  htsmsg_add_str(c, "class", mn->mn_id.in_class->ic_class);
  if (filename)
    snprintf(filename, size, "input/dab/networks/%s/config",
             idnode_uuid_as_str(&mn->mn_id, ubuf));
  return c;
}

static dab_ensemble_t *
dab_network_create_ensemble
  ( dab_network_t *mn, void *origin, uint32_t sid, void *aux, int force )
{
  return NULL;
}

static const idclass_t *
dab_network_ensemble_class
  ( dab_network_t *mn )
{
  extern const idclass_t dab_ensemble_class;
  return &dab_ensemble_class;
}

static dab_ensemble_t *
dab_network_ensemble_create2
  ( dab_network_t *mn, htsmsg_t *conf )
{
  return dab_ensemble_create1(NULL, mn, 0, conf);
}

void
dab_network_scan ( dab_network_t *mn )
{
  dab_ensemble_t *mm;
  LIST_FOREACH(mm, &mn->mn_ensembles, mm_network_link)
    dab_ensemble_scan_state_set(mm, MM_SCAN_STATE_PEND);
}

static void
dab_network_link_delete ( dab_network_link_t *mnl )
{
  idnode_notify_changed(&mnl->mnl_input->ti_id);
  LIST_REMOVE(mnl, mnl_mn_link);
  LIST_REMOVE(mnl, mnl_mi_link);
  free(mnl);
}

void
dab_network_delete
  ( dab_network_t *mn, int delconf )
{
  dab_ensemble_t *mm;
  dab_network_link_t *mnl;

  idnode_save_check(&mn->mn_id, delconf);

  /* Remove from global list */
  LIST_REMOVE(mn, mn_global_link);

  /* Delete all ensembles */
  while ((mm = LIST_FIRST(&mn->mn_ensembles))) {
    mm->mm_delete(mm, delconf);
  }

  /* Disarm scanning */
  mtimer_disarm(&mn->mn_scan_timer);

  /* Remove from input */
  while ((mnl = LIST_FIRST(&mn->mn_inputs)))
    dab_network_link_delete(mnl);

  /* Free memory */
  idnode_unlink(&mn->mn_id);
  free(mn->mn_network_name);
  free(mn);
}

void
dab_network_wizard_create
  ( const char *clazz, htsmsg_t **nlist, const char *lang )
{
  dab_network_t *mn;
  dab_network_builder_t *mnb;
  htsmsg_t *conf;

  if (nlist)
    *nlist = NULL;

  mnb = dab_network_builder_find(clazz);
  if (mnb == NULL)
    return;

  /* only one network per type */
  LIST_FOREACH(mn, &dab_network_all, mn_global_link)
    if (mn->mn_id.in_class == mnb->idc && mn->mn_wizard)
      goto found;

  conf = htsmsg_create_map();
  htsmsg_add_str(conf, "networkname", idclass_get_caption(mnb->idc, lang));
  htsmsg_add_bool(conf, "wizard", 1);
  mn = mnb->build(mnb->idc, conf);
  htsmsg_destroy(conf);
  if (mn)
    idnode_changed(&mn->mn_id);

found:
  if (mn && nlist) {
    *nlist = htsmsg_create_list();
    htsmsg_add_uuid(*nlist, NULL, &mn->mn_id.in_uuid);
  }
}

void
dab_network_get_type_str( dab_network_t *mn, char *buf, size_t buflen )
{
  const char *s = "DAB";
  snprintf(buf, buflen, "%s", s);
}


/******************************************************************************
 * Network classes/creation
 *****************************************************************************/

dab_network_builder_list_t dab_network_builders;

void
dab_network_register_builder
  ( const idclass_t *idc,
    dab_network_t *(*build) (const idclass_t *idc, htsmsg_t *conf) )
{
  dab_network_builder_t *mnb = calloc(1, sizeof(dab_network_builder_t));
  mnb->idc   = idc;
  mnb->build = build;
  LIST_INSERT_HEAD(&dab_network_builders, mnb, link);
  idclass_register(idc);
}

void
dab_network_unregister_builder
  ( const idclass_t *idc )
{
  dab_network_builder_t *mnb;
  LIST_FOREACH(mnb, &dab_network_builders, link) {
    if (mnb->idc == idc) {
      LIST_REMOVE(mnb, link);
      free(mnb);
      return;
    }
  }
}

dab_network_builder_t *
dab_network_builder_find
  ( const char *clazz )
{
  dab_network_builder_t *mnb;
  if (clazz == NULL)
    return NULL;
  LIST_FOREACH(mnb, &dab_network_builders, link) {
    if (!strcmp(mnb->idc->ic_class, clazz))
      return mnb;
  }
  return NULL;
}

dab_network_t *
dab_network_build
  ( const char *clazz, htsmsg_t *conf )
{
  dab_network_builder_t *mnb;
  mnb = dab_network_builder_find(clazz);
  if (mnb)
    return mnb->build(mnb->idc, conf);
  return NULL;
}

/* ****************************************************************************
 * Creation/Config
 * ***************************************************************************/

dab_network_list_t dab_network_all;

dab_network_t *
dab_network_create0
  ( dab_network_t *mn, const idclass_t *idc, const char *uuid,
    const char *netname, htsmsg_t *conf )
{
  char buf[256];
  dab_ensemble_t *mm;
  htsmsg_t *c, *e;
  htsmsg_field_t *f;

  /* Setup idnode */
  if (idnode_insert(&mn->mn_id, uuid, idc, 0)) {
    if (uuid)
      tvherror(LS_RTLSDR, "invalid network uuid '%s'", uuid);
    free(mn);
    return NULL;
  }

  /* Default callbacks */
  mn->mn_display_name   = dab_network_display_name;
  mn->mn_config_save    = dab_network_config_save;
  mn->mn_create_ensemble     = dab_network_create_ensemble;
  mn->mn_ensemble_class      = dab_network_ensemble_class;
  mn->mn_ensemble_create2    = dab_network_ensemble_create2;
  mn->mn_scan           = dab_network_scan;
  mn->mn_delete         = dab_network_delete;

  /* Add to global list */
  LIST_INSERT_HEAD(&dab_network_all, mn, mn_global_link);

  /* Initialise scanning */
  TAILQ_INIT(&mn->mn_scan_pend);
  TAILQ_INIT(&mn->mn_scan_ipend);
  TAILQ_INIT(&mn->mn_scan_active);
  mtimer_arm_rel(&mn->mn_scan_timer, dab_network_scan_timer_cb, mn, 0);

  /* Defaults */
  mn->mn_enabled = 1;
  mn->mn_skipinitscan = 1;
  mn->mn_autodiscovery = MN_DISCOVERY_NEW;

  /* Load config */
  if (conf)
    idnode_load(&mn->mn_id, conf);

  /* Name */
  if (netname) mn->mn_network_name = strdup(netname);
  mn->mn_display_name(mn, buf, sizeof(buf));
  tvhtrace(LS_RTLSDR, "created network %s", buf);

  /* No config */
  if (!conf)
    return mn;

  /* Load muxes */
  if ((c = hts_settings_load_r(1, "input/dab/networks/%s/ensembles", uuid))) {
    HTSMSG_FOREACH(f, c) {
      if (!(e = htsmsg_get_map_by_field(f)))  continue;
      if (!(e = htsmsg_get_map(e, "config"))) continue;
      mm = dab_ensemble_create1(htsmsg_field_name(f), mn, 0, e);
      dab_ensemble_post_create(mm);
    }
    htsmsg_destroy(c);
  }

  return mn;
}

static dab_network_t *
dab_network_builder( const idclass_t *idc, htsmsg_t *conf )
{
  return (dab_network_t*)dab_network_create(dab_network, NULL, NULL, conf);
}

void dab_network_init ( void )
{
  htsmsg_t *c, *e;
  htsmsg_field_t *f;
  const char *s;

  dab_network_register_builder(&dab_network_class, dab_network_builder);
  
    /* Load settings */
  if (!(c = hts_settings_load_r(1, "input/dab/networks")))
    return;

  HTSMSG_FOREACH(f, c) {
    if (!(e = htsmsg_get_map_by_field(f)))  continue;
    if (!(e = htsmsg_get_map(e, "config"))) continue;
    if (!(s = htsmsg_get_str(e, "class")))  continue;
    dab_network_create(dab_network, htsmsg_field_name(f), NULL, e);
  }
  htsmsg_destroy(c);
}

void dab_network_done ( void )
{
  tvh_mutex_lock(&global_lock);
  mpegts_network_unregister_builder(&dab_network_class);
  mpegts_network_class_delete(&dab_network_class, 0);
  tvh_mutex_unlock(&global_lock);
}
