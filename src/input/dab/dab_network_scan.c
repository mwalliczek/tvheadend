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

#include "input.h"

/******************************************************************************
 * Timer
 *****************************************************************************/

/* Notify */
static void
dab_network_scan_notify ( dab_ensemble_t *mm )
{
  idnode_notify_changed(&mm->mm_id);
  idnode_notify_changed(&mm->mm_network->mn_id);
}

/*
 * rules:
 *   1) prefer weight
 *   2) prefer ensemblees with the newer timestamp (scan interrupted?)
 *   3) do standard ensemble sorting
 */
static int
mm_cmp ( dab_ensemble_t *a, dab_ensemble_t *b )
{
  int r = b->mm_scan_weight - a->mm_scan_weight;
  if (r == 0) {
    int64_t l = b->mm_start_monoclock - a->mm_start_monoclock;
    if (l == 0)
      return dab_ensemble_compare(a, b);
    r = l > 0 ? 1 : -1;
  }
  return r;
}

/*
 * rules:
 *   1) prefer weight
 *   2) prefer ensemblees with the oldest timestamp
 *   3) do standard ensemble sorting
 */
static int
mm_cmp_idle ( dab_ensemble_t *a, dab_ensemble_t *b )
{
  int r = b->mm_scan_weight - a->mm_scan_weight;
  if (r == 0) {
    int64_t l = a->mm_start_monoclock - b->mm_start_monoclock;
    if (l == 0)
      return dab_ensemble_compare(a, b);
    r = l > 0 ? 1 : -1;
  }
  return r;
}

static void
dab_network_scan_queue_del0 ( dab_ensemble_t *mm )
{
  dab_network_t *mn = mm->mm_network;
  if (mm->mm_scan_state == MM_SCAN_STATE_ACTIVE) {
    TAILQ_REMOVE(&mn->mn_scan_active, mm, mm_scan_link);
  } else if (mm->mm_scan_state == MM_SCAN_STATE_PEND) {
    TAILQ_REMOVE(&mn->mn_scan_pend, mm, mm_scan_link);
  } else if (mm->mm_scan_state == MM_SCAN_STATE_IPEND) {
    TAILQ_REMOVE(&mn->mn_scan_ipend, mm, mm_scan_link);
  }
}

static int
dab_network_scan_do_ensemble ( dab_ensemble_queue_t *q, dab_ensemble_t *mm )
{
  int r, state = mm->mm_scan_state;

  assert(state == MM_SCAN_STATE_PEND ||
         state == MM_SCAN_STATE_IPEND ||
         state == MM_SCAN_STATE_ACTIVE);

  /* Don't try to subscribe already tuned ensemblees */
  if (mm->mm_active) return 0;

  /* Attempt to tune */
  r = dab_ensemble_subscribe(mm, NULL, "scan", mm->mm_scan_weight,
                           mm->mm_scan_flags |
                           SUBSCRIPTION_ONESHOT |
                           SUBSCRIPTION_TABLES);

  /* Started */
  state = mm->mm_scan_state;
  if (!r) {
    assert(state == MM_SCAN_STATE_ACTIVE);
    return 0;
  }
  assert(state == MM_SCAN_STATE_PEND ||
         state == MM_SCAN_STATE_IPEND);

  /* No free tuners - stop */
  if (r == SM_CODE_NO_FREE_ADAPTER || r == SM_CODE_NO_ADAPTERS)
    return -1;

  /* No valid tuners (subtly different, might be able to tuner a later
   * ensemble)
   */
  if (r == SM_CODE_NO_VALID_ADAPTER && mm->mm_is_enabled(mm))
    return 0;

  /* Failed */
  TAILQ_REMOVE(q, mm, mm_scan_link);
  if (mm->mm_scan_result != MM_SCAN_FAIL) {
    mm->mm_scan_result = MM_SCAN_FAIL;
    idnode_changed(&mm->mm_id);
  }
  mm->mm_scan_state  = MM_SCAN_STATE_IDLE;
  mm->mm_scan_weight = 0;
  dab_network_scan_notify(mm);
  return 0;
}

void
dab_network_scan_timer_cb ( void *p )
{
  dab_network_t *mn = p;
  dab_ensemble_t *mm, *nxt = NULL;

  /* Process standard Q */
  for (mm = TAILQ_FIRST(&mn->mn_scan_pend); mm != NULL; mm = nxt) {
    nxt = TAILQ_NEXT(mm, mm_scan_link);
    if (dab_network_scan_do_ensemble(&mn->mn_scan_pend, mm))
      break;
  }

  /* Process idle Q */
  for (mm = TAILQ_FIRST(&mn->mn_scan_ipend); mm != NULL; mm = nxt) {
    nxt = TAILQ_NEXT(mm, mm_scan_link);
    if (dab_network_scan_do_ensemble(&mn->mn_scan_ipend, mm))
      break;
  }

  /* Re-arm timer. Really this is just a safety measure as we'd normally
   * expect the timer to be forcefully triggered on finish of a ensemble scan
   */
  mtimer_arm_rel(&mn->mn_scan_timer, dab_network_scan_timer_cb, mn, sec2mono(120));
}

/******************************************************************************
 * Mux transition
 *****************************************************************************/

static void
dab_network_scan_ensemble_add ( dab_network_t *mn, dab_ensemble_t *mm )
{
  TAILQ_INSERT_SORTED_R(&mn->mn_scan_ipend, dab_ensemble_queue, mm,
                        mm_scan_link, mm_cmp);
  mtimer_arm_rel(&mn->mn_scan_timer, dab_network_scan_timer_cb,
                 mn, sec2mono(10));
}

static void
dab_network_scan_idle_ensemble_add ( dab_network_t *mn, dab_ensemble_t *mm )
{
  mm->mm_scan_weight = SUBSCRIPTION_PRIO_SCAN_IDLE;
  TAILQ_INSERT_SORTED_R(&mn->mn_scan_ipend, dab_ensemble_queue, mm,
                        mm_scan_link, mm_cmp_idle);
  mtimer_arm_rel(&mn->mn_scan_timer, dab_network_scan_timer_cb,
                 mn, sec2mono(10));
}

/* Finished */
static inline void
dab_network_scan_ensemble_done0
  ( dab_ensemble_t *mm, mpegts_mux_scan_result_t result, int weight )
{
  dab_network_t *mn = mm->mm_network;
  mpegts_mux_scan_state_t state = mm->mm_scan_state;

  if (result == MM_SCAN_OK || result == MM_SCAN_PARTIAL) {
    mm->mm_scan_last_seen = gclk();
    if (mm->mm_scan_first == 0)
      mm->mm_scan_first = mm->mm_scan_last_seen;
    idnode_changed(&mm->mm_id);
  }

  /* prevent double del: */
  /*   dab_ensemble_stop -> dab_network_scan_ensemble_cancel */
  mm->mm_scan_state = MM_SCAN_STATE_IDLE;
  dab_ensemble_unsubscribe_by_name(mm, "scan");
  mm->mm_scan_state = state;

  if (state == MM_SCAN_STATE_PEND || state == MM_SCAN_STATE_IPEND) {
    if (weight || mn->mn_idlescan) {
      dab_network_scan_queue_del0(mm);
      if (weight == 0 || weight == SUBSCRIPTION_PRIO_SCAN_IDLE)
        dab_network_scan_idle_ensemble_add(mn, mm);
      else
        dab_network_scan_ensemble_add(mn, mm);
      weight = 0;
    } else {
      dab_network_scan_queue_del(mm);
    }
  } else {
    if (weight == 0 && mn->mn_idlescan)
      weight = SUBSCRIPTION_PRIO_SCAN_IDLE;
    dab_network_scan_queue_del(mm);
  }

  if (result != MM_SCAN_NONE && mm->mm_scan_result != result) {
    mm->mm_scan_result = result;
    idnode_changed(&mm->mm_id);
  }

  /* Re-enable? */
  if (weight > 0)
    dab_network_scan_queue_add(mm, weight, mm->mm_scan_flags, 10);
}

/* Failed - couldn't start */
void
dab_network_scan_ensemble_fail ( dab_ensemble_t *mm )
{
  dab_network_scan_ensemble_done0(mm, MM_SCAN_FAIL, 0);
}

/* Completed succesfully */
void
dab_network_scan_ensemble_done ( dab_ensemble_t *mm )
{
  mm->mm_scan_flags = 0;
  dab_network_scan_ensemble_done0(mm, MM_SCAN_OK, 0);
}

/* Partially completed (not all tables were read) */
void
dab_network_scan_ensemble_partial ( dab_ensemble_t *mm )
{
  dab_network_scan_ensemble_done0(mm, MM_SCAN_PARTIAL, 0);
}

/* Interrupted (re-add) */
void
dab_network_scan_ensemble_cancel ( dab_ensemble_t *mm, int reinsert )
{
  if (reinsert) {
    if (mm->mm_scan_state != MM_SCAN_STATE_ACTIVE)
      return;
  } else {
    if (mm->mm_scan_state == MM_SCAN_STATE_IDLE)
      return;
    mm->mm_scan_flags = 0;
  }

  dab_network_scan_ensemble_done0(mm, MM_SCAN_NONE,
                                reinsert ? mm->mm_scan_weight : 0);
}

/* Mux has been started */
void
dab_network_scan_ensemble_active ( dab_ensemble_t *mm )
{
  dab_network_t *mn = mm->mm_network;
  if (mm->mm_scan_state != MM_SCAN_STATE_PEND &&
      mm->mm_scan_state != MM_SCAN_STATE_IPEND)
    return;
  dab_network_scan_queue_del0(mm);
  mm->mm_scan_state = MM_SCAN_STATE_ACTIVE;
  mm->mm_scan_init  = 0;
  TAILQ_INSERT_TAIL(&mn->mn_scan_active, mm, mm_scan_link);
}

/* Mux has been reactivated */
void
dab_network_scan_ensemble_reactivate ( dab_ensemble_t *mm )
{
  dab_network_t *mn = mm->mm_network;
  if (mm->mm_scan_state == MM_SCAN_STATE_ACTIVE)
    return;
  dab_network_scan_queue_del0(mm);
  mm->mm_scan_init  = 0;
  mm->mm_scan_state = MM_SCAN_STATE_ACTIVE;
  TAILQ_INSERT_TAIL(&mn->mn_scan_active, mm, mm_scan_link);
}

/******************************************************************************
 * Mux queue handling
 *****************************************************************************/

void
dab_network_scan_queue_del ( dab_ensemble_t *mm )
{
  dab_network_t *mn = mm->mm_network;
  tvhdebug(LS_MPEGTS, "removing ensemble %s from scan queue", mm->mm_nicename);
  dab_network_scan_queue_del0(mm);
  mm->mm_scan_state  = MM_SCAN_STATE_IDLE;
  mm->mm_scan_weight = 0;
  mtimer_disarm(&mm->mm_scan_timeout);
  mtimer_arm_rel(&mn->mn_scan_timer, dab_network_scan_timer_cb, mn, 0);
  dab_network_scan_notify(mm);
}

void
dab_network_scan_queue_add
  ( dab_ensemble_t *mm, int weight, int flags, int delay )
{
  int requeue = 0;
  dab_network_t *mn = mm->mm_network;

  if (!mm->mm_is_enabled(mm)) return;

  if (weight <= 0) return;

  if (weight > mm->mm_scan_weight) {
    mm->mm_scan_weight = weight;
    requeue = 1;
  }

  /* Already active */
  if (mm->mm_scan_state == MM_SCAN_STATE_ACTIVE)
    return;

  /* Remove entry (or ignore) */
  if (mm->mm_scan_state == MM_SCAN_STATE_PEND ||
      mm->mm_scan_state == MM_SCAN_STATE_IPEND) {
    if (!requeue)
      return;
    dab_network_scan_queue_del0(mm);
  }

  tvhdebug(LS_MPEGTS, "adding ensemble %s to scan queue weight %d flags %04X",
                      mm->mm_nicename, weight, flags);

  /* Add new entry */
  mm->mm_scan_flags |= flags;
  if (mm->mm_scan_flags == 0)
    mm->mm_scan_flags = SUBSCRIPTION_IDLESCAN;
  if (weight == SUBSCRIPTION_PRIO_SCAN_IDLE) {
    mm->mm_scan_state  = MM_SCAN_STATE_IPEND;
    TAILQ_INSERT_SORTED_R(&mn->mn_scan_ipend, dab_ensemble_queue,
                          mm, mm_scan_link, mm_cmp_idle);
  } else {
    mm->mm_scan_state  = MM_SCAN_STATE_PEND;
    TAILQ_INSERT_SORTED_R(&mn->mn_scan_pend, dab_ensemble_queue,
                          mm, mm_scan_link, mm_cmp);
  }
  mtimer_arm_rel(&mn->mn_scan_timer, dab_network_scan_timer_cb, mn, sec2mono(delay));
  dab_network_scan_notify(mm);
}

/******************************************************************************
 * Subsystem setup / tear down
 *****************************************************************************/

void
dab_network_scan_init ( void )
{
}

void
dab_network_scan_done ( void )
{
}

/******************************************************************************
 * Editor Configuration
 *
 * vim:sts=2:ts=2:sw=2:et
 *****************************************************************************/
