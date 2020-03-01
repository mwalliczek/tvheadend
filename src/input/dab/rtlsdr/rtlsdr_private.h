/*
 *  Tvheadend - RTL SDR Input System
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

#ifndef __TVH_RTLSDR_PRIVATE_H__
#define __TVH_RTLSDR_PRIVATE_H__

#include <rtl-sdr.h>

#include "input.h"
#include "dab.h"

typedef struct rtlsdr_adapter     rtlsdr_adapter_t;
typedef struct rtlsdr_frontend    rtlsdr_frontend_t;

struct rtlsdr_adapter
{
	tvh_hardware_t;

	/*
	* Adapter info
	*/
	int	dev_index;
	char    *device_name;
	char    *vendor;
	char    *product;
	char    *serial;

	/*
	 * Frontends
	 */
	LIST_HEAD(, rtlsdr_frontend) la_frontends;

	/*
	* Functions
	*/
	int(*la_is_enabled) (rtlsdr_adapter_t *la);
};

struct rtlsdr_frontend
{
	dab_input_t;

	/*
	* Adapter
	*/
	rtlsdr_adapter_t           *lfe_adapter;
	LIST_ENTRY(rtlsdr_frontend) lfe_link;

	/*
	* Reception
	*/
	rtlsdr_dev_t *dev;
	struct sdr_state_t sdr;
	pthread_t demod_thread;
	pthread_t read_thread;
	th_pipe_t                 lfe_dvr_pipe;
	th_pipe_t                 lfe_control_pipe;
	int running;
	
	/*
	* Tuning
	*/
	int                       lfe_refcount;
	int                       lfe_ready;
	int                       lfe_in_setup;
	int                       lfe_locked;
	int                       lfe_status;
	int                       lfe_freq;
	time_t                    lfe_monitor;
	mtimer_t                  lfe_monitor_timer;
	
	uint32_t                  lfe_status_period;
};

extern const idclass_t rtlsdr_adapter_class;
extern const idclass_t rtlsdr_frontend_dab_class;

void rtlsdr_init(void);

void rtlsdr_done(void);

static inline void rtlsdr_adapter_changed(rtlsdr_adapter_t *la)
{
	idnode_changed(&la->th_id);
}

rtlsdr_frontend_t * rtlsdr_frontend_create(htsmsg_t *conf, rtlsdr_adapter_t *la);

int
rtlsdr_frontend_get_weight(dab_input_t *mi, dab_ensemble_t *mm, int flags, int weight );

void rtlsdr_frontend_save(rtlsdr_frontend_t *lfe, htsmsg_t *m);

void rtlsdr_frontend_destroy(rtlsdr_frontend_t *lfe);

int rtlsdr_frontend_clear(rtlsdr_frontend_t *lfe, dab_ensemble_instance_t *mmi);

int rtlsdr_frontend_tune0(rtlsdr_frontend_t *lfe, dab_ensemble_instance_t *mmi, uint32_t freq);

int rtlsdr_frontend_tune1(rtlsdr_frontend_t *lfe, dab_ensemble_instance_t *mmi, uint32_t freq);

void rtlsdr_frontend_create_ensemble_instance0(rtlsdr_frontend_t *lfe, dab_ensemble_t *mm);

void rtlsdr_frontend_set_enabled ( rtlsdr_frontend_t *mi, int enabled );

uint32_t getSamples(rtlsdr_frontend_t *lfe, float _Complex *v, uint32_t size, int32_t freqOffset);
uint32_t getSample(rtlsdr_frontend_t *lfe, float _Complex *v, float *abs, int32_t freqOffset);

#endif /* __TVH_RTLSDR_PRIVATE_H__ */


