/*
 *  Tvheadend - Network Scanner
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

#ifndef __TVH_DAB_NETWORK_SCAN_H__
#define __TVH_DAB_NETWORK_SCAN_H__

#include "tvheadend.h"
#include "cron.h"
#include "idnode.h"
#include "subscriptions.h"

/*
 * Timer callback (only to be used in network init)
 */
void dab_network_scan_timer_cb ( void *p );

/*
 * Registration functions
 */
void dab_network_scan_queue_add ( dab_ensemble_t *mm, int weight,
                                     int flags, int delay );
void dab_network_scan_queue_del ( dab_ensemble_t *mm );

/*
 * Events
 */
void dab_network_scan_ensemble_fail    ( dab_ensemble_t *mm );
void dab_network_scan_ensemble_done    ( dab_ensemble_t *mm );
void dab_network_scan_ensemble_partial ( dab_ensemble_t *mm );
void dab_network_scan_ensemble_cancel  ( dab_ensemble_t *mm, int reinsert );
void dab_network_scan_ensemble_active  ( dab_ensemble_t *mm );
void dab_network_scan_ensemble_reactivate ( dab_ensemble_t *mm );

/*
 * Init / Teardown
 */
void dab_network_scan_init ( void );
void dab_network_scan_done ( void );

#endif /* __TVH_DAB_NETWORK_SCAN_H__*/

/******************************************************************************
 * Editor Configuration
 *
 * vim:sts=2:ts=2:sw=2:et
 *****************************************************************************/
