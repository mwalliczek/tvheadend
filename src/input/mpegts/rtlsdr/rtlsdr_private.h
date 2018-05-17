#ifndef __TVH_RTLSDR_PRIVATE_H__
#define __TVH_RTLSDR_PRIVATE_H__

#include <rtl-sdr.h>
#include <semaphore.h>

#include "input.h"
#include "dab.h"
#include "input_sdr.h"

typedef struct rtlsdr_hardware    rtlsdr_hardware_t;
typedef struct rtlsdr_adapter     rtlsdr_adapter_t;
typedef struct rtlsdr_frontend    rtlsdr_frontend_t;

struct rtlsdr_adapter
{
	tvh_hardware_t;

	/*
	* Adapter info
	*/
	int		 dev_index;
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
	mpegts_input_t;

	/*
	* Adapter
	*/
	rtlsdr_adapter_t           *lfe_adapter;
	LIST_ENTRY(rtlsdr_frontend) lfe_link;

	/*
	* Reception
	*/
	rtlsdr_dev_t *dev;
	struct dab_state_t *dab;
	pthread_t demod_thread;
	sem_t data_ready;

	/*
	* Tuning
	*/
	int                       lfe_refcount;
	int                       lfe_ready;
	int						  lfe_reading;
	int                       lfe_in_setup;
	int                       lfe_locked;
	int                       lfe_status;
	int                       lfe_freq;
	time_t                    lfe_monitor;
	mtimer_t                  lfe_monitor_timer;
};

extern const idclass_t rtlsdr_adapter_class;
extern const idclass_t rtlsdr_frontend_dab_class;

void rtlsdr_init(void);

void rtlsdr_done(void);

static inline void rtlsdr_adapter_changed(rtlsdr_adapter_t *la)
{
	idnode_changed(&la->th_id);
}

rtlsdr_frontend_t *
rtlsdr_frontend_create
(htsmsg_t *conf, rtlsdr_adapter_t *la);

void rtlsdr_frontend_save(rtlsdr_frontend_t *lfe, htsmsg_t *m);

void rtlsdr_frontend_destroy(rtlsdr_frontend_t *lfe);

int rtlsdr_frontend_tune
(rtlsdr_frontend_t *lfe, mpegts_mux_instance_t *mmi, uint32_t freq);

#endif /* __TVH_RTLSDR_PRIVATE_H__ */


