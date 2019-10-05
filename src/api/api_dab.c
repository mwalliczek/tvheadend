/*
 *  tvheadend - API access to MPEGTS system
 *
 *  Copyright (C) 2013 Adam Sutton
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

#include "tvheadend.h"
#include "access.h"
#include "htsmsg.h"
#include "api.h"
#include "input.h"

/*
 * Inputs
 */
static int
api_dab_input_network_list
  ( access_t *perm, void *opaque, const char *op, htsmsg_t *args, htsmsg_t **resp )
{
  int i, err = EINVAL;
  const char *uuid;
  dab_input_t *mi;
  dab_network_t *mn;
  idnode_set_t *is;
  char ubuf[UUID_HEX_SIZE];
  extern const idclass_t dab_input_class;

  if (!(uuid = htsmsg_get_str(args, "uuid")))
    return EINVAL;

  tvh_mutex_lock(&global_lock);

  mi = dab_input_find(uuid);
  if (!mi)
    goto exit;

  htsmsg_t     *l  = htsmsg_create_list();
  if ((is = mi->mi_network_list(mi))) {
    for (i = 0; i < is->is_count; i++) {
      char buf[256];
      mn = (dab_network_t*)is->is_array[i];
      mn->mn_display_name(mn, buf, sizeof(buf));
      htsmsg_add_msg(l, NULL, htsmsg_create_key_val(idnode_uuid_as_str(&mn->mn_id, ubuf), buf));
    }
    idnode_set_free(is);
  }
  err   = 0;
  *resp = htsmsg_create_map();
  htsmsg_add_msg(*resp, "entries", l);

exit:
  tvh_mutex_unlock(&global_lock);

  return err;
}

/*
 * Networks
 */
static void
api_dab_network_grid
  ( access_t *perm, idnode_set_t *ins, api_idnode_grid_conf_t *conf, htsmsg_t *args )
{
  dab_network_t *mn;

  LIST_FOREACH(mn, &dab_network_all, mn_global_link) {
    idnode_set_add(ins, (idnode_t*)mn, &conf->filter, perm->aa_lang_ui);
  }
}

static int
api_dab_network_builders
  ( access_t *perm, void *opaque, const char *op, htsmsg_t *args, htsmsg_t **resp )
{
  dab_network_builder_t *mnb;
  htsmsg_t *l, *e;

  /* List of available builder classes */
  l = htsmsg_create_list();
  LIST_FOREACH(mnb, &dab_network_builders, link)
    if ((e = idclass_serialize(mnb->idc, perm->aa_lang_ui)))
      htsmsg_add_msg(l, NULL, e);

  /* Output */
  *resp = htsmsg_create_map();  
  htsmsg_add_msg(*resp, "entries", l);

  return 0;
}

static int
api_dab_network_create
  ( access_t *perm, void *opaque, const char *op, htsmsg_t *args, htsmsg_t **resp )
{
  int err;
  const char *class;
  htsmsg_t *conf;
  dab_network_t *mn;

  if (!(class = htsmsg_get_str(args, "class")))
    return EINVAL;
  if (!(conf  = htsmsg_get_map(args, "conf")))
    return EINVAL;

  tvh_mutex_lock(&global_lock);
  mn = dab_network_build(class, conf);
  if (mn) {
    err = 0;
    api_idnode_create(resp, &mn->mn_id);
  } else {
    err = EINVAL;
  }
  tvh_mutex_unlock(&global_lock);

  return err;
}

static int
api_dab_network_scan
  ( access_t *perm, void *opaque, const char *op, htsmsg_t *args, htsmsg_t **resp )
{
  htsmsg_field_t *f;
  htsmsg_t *uuids;
  dab_network_t *mn;
  const char *uuid;

  if (!(f = htsmsg_field_find(args, "uuid")))
    return -EINVAL;
  if ((uuids = htsmsg_field_get_list(f))) {
    HTSMSG_FOREACH(f, uuids) {
      if (!(uuid = htsmsg_field_get_str(f))) continue;
      tvh_mutex_lock(&global_lock);
      mn = dab_network_find(uuid);
      if (mn && mn->mn_scan)
        mn->mn_scan(mn);
      tvh_mutex_unlock(&global_lock);
    }
  } else if ((uuid = htsmsg_field_get_str(f))) {
    tvh_mutex_lock(&global_lock);
    mn = dab_network_find(uuid);
    if (mn && mn->mn_scan)
      mn->mn_scan(mn);
    tvh_mutex_unlock(&global_lock);
    if (!mn)
      return -ENOENT;
  } else {
    return -EINVAL;
  }

  return 0;
}

static int
api_dab_network_ensembleclass
  ( access_t *perm, void *opaque, const char *op, htsmsg_t *args, htsmsg_t **resp )
{
  int err = EINVAL;
  const idclass_t *idc; 
  dab_network_t *mn;
  const char *uuid;

  if (!(uuid = htsmsg_get_str(args, "uuid")))
    return EINVAL;
  
  tvh_mutex_lock(&global_lock);
  
  if (!(mn  = dab_network_find(uuid)))
    goto exit;

  if (!(idc = mn->mn_ensemble_class(mn)))
    goto exit;

  *resp = idclass_serialize(idc, perm->aa_lang_ui);
  err    = 0;

exit:
  tvh_mutex_unlock(&global_lock);
  return err;
}

static int
api_dab_network_ensemblecreate
  ( access_t *perm, void *opaque, const char *op, htsmsg_t *args, htsmsg_t **resp )
{
  int err = EINVAL;
  dab_network_t *mn;
  dab_ensemble_t *mm;
  htsmsg_t *conf;
  const char *uuid;

  if (!(uuid = htsmsg_get_str(args, "uuid")))
    return EINVAL;
  if (!(conf = htsmsg_get_map(args, "conf")))
    return EINVAL;
  
  tvh_mutex_lock(&global_lock);
  
  if (!(mn  = dab_network_find(uuid)))
    goto exit;
  
  if (!(mm = mn->mn_ensemble_create2(mn, conf)))
    goto exit;

  api_idnode_create(resp, &mm->mm_id);
  err = 0;

exit:
  tvh_mutex_unlock(&global_lock);
  return err;
}

/*
 * Muxes
 */
static void
api_dab_ensemble_grid
  ( access_t *perm, idnode_set_t *ins, api_idnode_grid_conf_t *conf, htsmsg_t *args )
{
  dab_network_t *mn;
  dab_ensemble_t *mm;
  int hide = 1;
  const char *s = htsmsg_get_str(args, "hidemode");
  if (s) {
    if (!strcmp(s, "all"))
      hide = 2;
    else if (!strcmp(s, "none"))
      hide = 0;
  }

  LIST_FOREACH(mn, &dab_network_all, mn_global_link) {
    //if (hide && !mn->mn_enabled) continue;
    LIST_FOREACH(mm, &mn->mn_ensembles, mm_network_link) {
      if (hide == 2 && !mm->mm_is_enabled(mm)) continue;
      idnode_set_add(ins, (idnode_t*)mm, &conf->filter, perm->aa_lang_ui);
    }
  }
}

/*
 * Services
 */
static void
api_dab_service_grid
  ( access_t *perm, idnode_set_t *ins, api_idnode_grid_conf_t *conf, htsmsg_t *args )
{
  dab_network_t *mn;
  dab_ensemble_t *mm;
  dab_service_t *ms;
  int hide = 1;
  const char *s = htsmsg_get_str(args, "hidemode");
  if (s) {
    if (!strcmp(s, "all"))
      hide = 2;
    else if (!strcmp(s, "none"))
      hide = 0;
  }

  LIST_FOREACH(mn, &dab_network_all, mn_global_link) {
    //if (hide && !mn->mn_enabled) continue;
    LIST_FOREACH(mm, &mn->mn_ensembles, mm_network_link) {
      if (hide && !mm->mm_is_enabled(mm)) continue;
      LIST_FOREACH(ms, &mm->mm_services, s_dab_ensemble_link) {
        if (hide && !ms->s_verified) continue;
        if (hide == 2 && !ms->s_is_enabled((service_t*)ms, 0)) continue;
        idnode_set_add(ins, (idnode_t*)ms, &conf->filter, perm->aa_lang_ui);
      }
    }
  }
}

/*
 * Init
 */
void
api_dab_init ( void )
{
  extern const idclass_t dab_network_class;
  extern const idclass_t dab_ensemble_class;
  extern const idclass_t dab_service_class;

  static api_hook_t ah[] = {
    { "dab/input/network_list", ACCESS_ADMIN, api_dab_input_network_list, NULL },
    { "dab/network/grid",       ACCESS_ADMIN, api_idnode_grid,  api_dab_network_grid },
    { "dab/network/class",      ACCESS_ADMIN, api_idnode_class, (void*)&dab_network_class },
    { "dab/network/builders",   ACCESS_ADMIN, api_dab_network_builders, NULL },
    { "dab/network/create",     ACCESS_ADMIN, api_dab_network_create,   NULL },
    { "dab/network/mux_class",  ACCESS_ADMIN, api_dab_network_ensembleclass, NULL },
    { "dab/network/mux_create", ACCESS_ADMIN, api_dab_network_ensemblecreate, NULL },
    { "dab/network/scan",       ACCESS_ADMIN, api_dab_network_scan, NULL },
    { "dab/mux/grid",           ACCESS_ADMIN, api_idnode_grid,  api_dab_ensemble_grid },
    { "dab/mux/class",          ACCESS_ADMIN, api_idnode_class, (void*)&dab_ensemble_class },
    { "dab/service/grid",       ACCESS_ADMIN, api_idnode_grid,  api_dab_service_grid },
    { "dab/service/class",      ACCESS_ADMIN, api_idnode_class, (void*)&dab_service_class },
    { NULL },
  };

  api_register_all(ah);
}
