#include <fcntl.h>

#include "tvheadend.h"
#include "input.h"
#include "rtlsdr_private.h"
#include "phasereference.h"
#include "ofdmDecoder.h"
#include "ficHandler.h"
#include "sdr_dab_basic_demodulation.h"

#define DEFAULT_ASYNC_BUF_NUMBER 32

static void
rtlsdr_frontend_monitor(void *aux);

/* **************************************************************************
* Class definition
* *************************************************************************/

static void
rtlsdr_frontend_class_changed(idnode_t *in)
{
	rtlsdr_adapter_t *la = ((rtlsdr_frontend_t*)in)->lfe_adapter;
	rtlsdr_adapter_changed(la);
}

CLASS_DOC(rtlsdr_frontend)

const idclass_t rtlsdr_frontend_class =
{
	.ic_super = &dab_input_class,
	.ic_class = "rtlsdr_frontend",
	.ic_caption = N_("RTL SDR frontend"),
	.ic_doc = tvh_doc_rtlsdr_frontend_class,
	.ic_changed = rtlsdr_frontend_class_changed,
	.ic_properties = (const property_t[]) {
		{
		}
	}
};


const idclass_t rtlsdr_frontend_dab_class =
{
	.ic_super = &rtlsdr_frontend_class,
	.ic_class = "rtlsdr_frontend_dab",
	.ic_caption = N_("TV Adapters - RTL SDR DAB Frontend"),
	.ic_properties = (const property_t[]) {
		{}
}
};

/* **************************************************************************
* Class methods
* *************************************************************************/

static void
rtlsdr_frontend_close_fd(rtlsdr_frontend_t *lfe)
{
	if (lfe->dev == NULL)
		return;

	tvhdebug(LS_RTLSDR, "closing FE %d", lfe->lfe_adapter->dev_index);
	rtlsdr_close(lfe->dev);
	lfe->dev = NULL;
}

static int
rtlsdr_frontend_open_fd(rtlsdr_frontend_t *lfe)
{
	int r;

	if (lfe->dev != NULL)
		return 0;

	if (lfe->dev == NULL) {
		r = rtlsdr_open(&lfe->dev, lfe->lfe_adapter->dev_index);
		if (r < 0) {
			tvherror(LS_RTLSDR, "Failed to open rtlsdr device #%d.\n", lfe->lfe_adapter->dev_index);
			exit(1);
		}
	}

	tvhdebug(LS_RTLSDR, "opening FE %d", lfe->lfe_adapter->dev_index);

	return lfe->dev == NULL;
}

static void
rtlsdr_frontend_enabled_updated(dab_input_t *ti)
{
	rtlsdr_frontend_t *lfe = (rtlsdr_frontend_t *)ti;

	/* Ensure disabled */
	if (!lfe->mi_enabled) {
		tvhtrace(LS_RTLSDR, "%d - disabling tuner", lfe->lfe_adapter->dev_index);
		rtlsdr_frontend_close_fd(lfe);
	}
	else if (lfe->dev == NULL) {

		rtlsdr_frontend_open_fd(lfe);

	}
}

static int
rtlsdr_frontend_is_enabled
(dab_input_t *mi, dab_ensemble_t *mm, int flags, int weight)
{
	rtlsdr_frontend_t *lfe = (rtlsdr_frontend_t*)mi;
	int w;

	if (lfe->lfe_adapter == NULL)
		return MI_IS_ENABLED_NEVER;
	w = dab_input_is_enabled(mi, mm, flags, weight);
	if (w != MI_IS_ENABLED_OK)
		return w;
	if (mm == NULL)
		return MI_IS_ENABLED_OK;
	if (lfe->lfe_in_setup)
		return MI_IS_ENABLED_RETRY;
	return MI_IS_ENABLED_OK;
}

static void
rtlsdr_frontend_stop_ensemble
(dab_input_t *mi, dab_ensemble_instance_t *mmi)
{
	char buf1[256];
	rtlsdr_frontend_t *lfe = (rtlsdr_frontend_t*)mi;

	mi->mi_display_name(mi, buf1, sizeof(buf1));
	tvhdebug(LS_RTLSDR, "%s - stopping %s", buf1, mmi->mmi_ensemble->mm_nicename);

	/* Stop thread */
	if (lfe->lfe_dvr_pipe.wr > 0) {
		tvh_write(lfe->lfe_dvr_pipe.wr, "", 1);
		lfe->running = 0;
		tvhtrace(LS_RTLSDR, "%s - waiting for dvr thread", buf1);
		pthread_join(lfe->demod_thread, NULL);
		tvh_pipe_close(&lfe->lfe_dvr_pipe);
		tvhdebug(LS_RTLSDR, "%s - stopped dvr thread", buf1);
		pthread_join(lfe->read_thread, NULL);
		tvh_pipe_close(&lfe->lfe_control_pipe);
	}
	
	sdr_destroy(&lfe->sdr);

	/* Not locked */
	lfe->lfe_ready = 0;
	lfe->lfe_locked = 0;
	lfe->lfe_status = 0;

	/* Ensure it won't happen immediately */
	mtimer_arm_rel(&lfe->lfe_monitor_timer, rtlsdr_frontend_monitor, lfe, sec2mono(2));

	lfe->lfe_refcount--;
	lfe->lfe_in_setup = 0;
	lfe->lfe_freq = 0;
}

static int
rtlsdr_frontend_warm_ensemble(dab_input_t *ti, dab_ensemble_instance_t *mmi)
{
	rtlsdr_frontend_t *lfe = (rtlsdr_frontend_t *) ti;
	rtlsdr_frontend_t *lfe2 = NULL;
	dab_ensemble_instance_t *lmmi = NULL;

	dab_ensemble_instance_t *cur;

	cur = LIST_FIRST(&lfe->mi_ensemble_active);
	if (cur != NULL) {
		/* Already tuned */
		if (mmi == cur)
			return 0;

		/* Stop current */
		cur->mmi_ensemble->mm_stop(cur->mmi_ensemble, 1, SM_CODE_SUBSCRIPTION_OVERRIDDEN);
	}
	if (LIST_FIRST(&lfe->mi_ensemble_active))
		return SM_CODE_TUNING_FAILED;

	/* Stop other active frontend (should be only one) */
	LIST_FOREACH(lfe2, &lfe->lfe_adapter->la_frontends, lfe_link) {
		if (lfe2 == lfe) continue;
		lmmi = LIST_FIRST(&lfe2->mi_ensemble_active);
		if (lmmi) {
			/* Stop */
			lmmi->mmi_ensemble->mm_stop(lmmi->mmi_ensemble, 1, SM_CODE_ABORTED);
		}
		rtlsdr_frontend_close_fd(lfe2);
	}
	return 0;
}

static int rtlsdr_frontend_start_ensemble(dab_input_t *ti, dab_ensemble_instance_t *mmi, int weight)
{
	rtlsdr_frontend_t *lfe = (rtlsdr_frontend_t *) ti;
	int res;

	assert(lfe->lfe_in_setup == 0);

	lfe->lfe_refcount++;
	lfe->lfe_in_setup = 1;

	res = rtlsdr_frontend_tune1(lfe, mmi, -1);

	if (res) {
		lfe->lfe_in_setup = 0;
		lfe->lfe_refcount--;
	}
	return res;
}

static void rtlsdr_frontend_create_ensemble_instance(dab_input_t *ti, dab_ensemble_t *mm)
{
  rtlsdr_frontend_t *lfe = (rtlsdr_frontend_t *) ti;
  tvh_hardware_t *th;
  rtlsdr_adapter_t *la;
  rtlsdr_frontend_t *lfe2;
  char ubuf[UUID_HEX_SIZE];

  idnode_uuid_as_str(&lfe->ti_id, ubuf);
  rtlsdr_frontend_create_ensemble_instance0(lfe, mm);
  /* create the instances for the slaves */
  LIST_FOREACH(th, &tvh_hardware, th_link) {
    if (!idnode_is_instance(&th->th_id, &rtlsdr_adapter_class)) continue;
    la = (rtlsdr_adapter_t*)th;
    LIST_FOREACH(lfe2, &la->la_frontends, lfe_link)
      rtlsdr_frontend_create_ensemble_instance0(lfe2, mm);
  }
}

void
rtlsdr_frontend_create_ensemble_instance0(rtlsdr_frontend_t *lfe, dab_ensemble_t *mm)
{
  tvh_input_instance_t *tii;
  LIST_FOREACH(tii, &lfe->mi_ensemble_instances, tii_input_link)
    if (((dab_ensemble_instance_t *)tii)->mmi_ensemble == mm) break;
  if (!tii)
    dab_ensemble_instance_create(dab_ensemble_instance, NULL, (dab_input_t *)lfe, mm);
}

static void rtlsdr_frontend_open_service(dab_input_t *di, dab_service_t *s, int flags, int first, int weight)
{
	rtlsdr_frontend_t *lfe = (rtlsdr_frontend_t *) di;
	tvhtrace(LS_RTLSDR, "open service %s", s->s_nicename);
	dab_input_open_service(di, s, flags, first, weight);
	if (s->s_type != STYPE_RAW) {
		sdr_dab_service_instance_t *sds = sdr_dab_service_instance_create(s);
		
		tvh_mutex_lock(&lfe->sdr.active_service_mutex);
		LIST_INSERT_HEAD(&lfe->sdr.active_service_instance, sds, service_link);
		tvh_mutex_unlock(&lfe->sdr.active_service_mutex);
	}
}

static void rtlsdr_frontend_close_service(dab_input_t *di, dab_service_t *s)
{
	rtlsdr_frontend_t *lfe = (rtlsdr_frontend_t *) di;
	sdr_dab_service_instance_t *sds;
	tvhtrace(LS_RTLSDR, "close service %s", s->s_nicename);
	if (s->s_type != STYPE_RAW) {
		tvh_mutex_lock(&lfe->sdr.active_service_mutex);
		LIST_FOREACH(sds, &lfe->sdr.active_service_instance, service_link) {
			if (sds->dai_service == s) {
				LIST_REMOVE(sds, service_link);
				sdr_dab_service_instance_destroy(sds);
				break;
			}
		}
		tvh_mutex_unlock(&lfe->sdr.active_service_mutex);
	}
	dab_input_close_service(di, s);
}

static idnode_set_t *
rtlsdr_frontend_network_list(dab_input_t *mi)
{
	tvhtrace(LS_RTLSDR, "%s: network list for DAB", mi->mi_name ? : "");

	return idnode_find_all(&dab_network_class, NULL);
}

static void rtlsdr_dab_callback(uint8_t *buf, uint32_t len, void *ctx)
{
	rtlsdr_frontend_t *lfe = ctx;
	struct sdr_state_t *sdr = &lfe->sdr;
	tvhtrace(LS_RTLSDR, "callback with %u bytes, count %u", len, sdr->fifo.count);
	if (!ctx) {
		return;
	}
#ifdef TRACE_RTLSDR_RAW
	fwrite(buf, sizeof(uint8_t), len, sdr->traceFile);
#endif
	if (lfe->lfe_dvr_pipe.wr < 0 || !lfe->running || !tvheadend_is_running()) {
		rtlsdr_cancel_async(lfe->dev);
		return;
	}
	/* write input data into fifo */
	cbWrite(&(sdr->fifo), buf, len);
	tvh_write(lfe->lfe_control_pipe.wr, "", 1);
}

static void *rtlsdr_read_thread_fn(void *arg)
{
	rtlsdr_frontend_t *lfe = arg;
	rtlsdr_read_async(lfe->dev, rtlsdr_dab_callback, (void *)lfe,
		DEFAULT_ASYNC_BUF_NUMBER, DEFAULT_BUF_LENGTH);
	return 0;
}

static void
rtlsdr_frontend_monitor(void *aux)
{
	char buf[256];
	rtlsdr_frontend_t *lfe = aux;
	dab_ensemble_instance_t *mmi = LIST_FIRST(&lfe->mi_ensemble_active);
//	mpegts_mux_t *mm;
//	service_t *s;
	uint32_t period = MINMAX(lfe->lfe_status_period, 250, 8000);
	struct sdr_state_t *sdr;
	signal_state_t status;
	signal_status_t sigstat;
	streaming_message_t sm;
	service_t *s;

	lfe->mi_display_name((dab_input_t*)lfe, buf, sizeof(buf));
	tvhtrace(LS_RTLSDR, "%s - checking FE status%s", buf, lfe->lfe_ready ? " (ready)" : "");

	/* Disabled */
	if (!lfe->mi_enabled && mmi)
		mmi->mmi_ensemble->mm_stop(mmi->mmi_ensemble, 1, SM_CODE_ABORTED);

	/* Close FE */
	if (lfe->dev != NULL && !lfe->lfe_refcount) {
		rtlsdr_frontend_close_fd(lfe);
		return;
	}

	/* Stop timer */
	if (!mmi || !lfe->lfe_ready) return;

	/* re-arm */
	mtimer_arm_rel(&lfe->lfe_monitor_timer, rtlsdr_frontend_monitor, lfe, ms2mono(period));

	/* Get current mux */
//	mm = mmi->mmi_mux;

	/* Waiting for lock */
	if (lfe->lfe_dvr_pipe.wr <= 0) {

		lfe->running = 1;
		/* Start input */
		tvh_pipe(O_NONBLOCK, &lfe->lfe_dvr_pipe);
		sdr->mmi->tii_stats.snr_scale = SIGNAL_STATUS_SCALE_DECIBEL;
		sdr->mmi->tii_stats.signal_scale = SIGNAL_STATUS_SCALE_RELATIVE;
		tvh_pipe(O_NONBLOCK, &lfe->lfe_control_pipe);

		sdr->frequency = lfe->lfe_freq;

		rtlsdr_reset_buffer(lfe->dev);
		tvh_thread_create(&lfe->demod_thread, NULL,
			rtlsdr_demod_thread_fn, lfe, "rtlsdr-front");
		tvh_thread_create(&lfe->read_thread, NULL,
			rtlsdr_read_thread_fn, lfe, "rtlsdr-front-read");

	} else  {
		lfe->lfe_locked = lfe->sdr.mmi->fibProcessorIsSynced;
		status = lfe->sdr.isSynced ? SIGNAL_GOOD : SIGNAL_NONE;

		mmi->tii_stats.signal = lfe->sdr.sLevel * 32768.0;

		/* Send message */
		sigstat.status_text = signal2str(status);
		sigstat.snr = mmi->tii_stats.snr;
		sigstat.signal = mmi->tii_stats.signal;
/*		sigstat.ber = mmi->tii_stats.ber;
		sigstat.unc = atomic_get(&mmi->tii_stats.unc);*/
		sigstat.signal_scale = mmi->tii_stats.signal_scale;
		sigstat.snr_scale = mmi->tii_stats.snr_scale;
/*		sigstat.ec_bit = mmi->tii_stats.ec_bit;
		sigstat.tc_bit = mmi->tii_stats.tc_bit;
		sigstat.ec_block = mmi->tii_stats.ec_block;
		sigstat.tc_block = mmi->tii_stats.tc_block; */
		memset(&sm, 0, sizeof(sm));
		sm.sm_type = SMT_SIGNAL_STATUS;
		sm.sm_data = &sigstat;

		LIST_FOREACH(s, &mmi->mmi_ensemble->mm_transports, s_active_link) {
			tvhtrace(LS_RTLSDR, "sending streaming statistics to %s", s->s_nicename);
			tvh_mutex_lock(&s->s_stream_mutex);
			streaming_service_deliver(s, streaming_msg_clone(&sm));
			tvh_mutex_unlock(&s->s_stream_mutex);
		}
	}
}

/* **************************************************************************
* Tuning
* *************************************************************************/

int
rtlsdr_frontend_clear
(rtlsdr_frontend_t *lfe, dab_ensemble_instance_t *mmi)
{
  char buf1[256];
  struct sdr_state_t *sdr;

  /* Open FE */
  lfe->mi_display_name((dab_input_t*)lfe, buf1, sizeof(buf1));
  tvhdebug(LS_RTLSDR, "%s - frontend clear", buf1);

  if (rtlsdr_frontend_open_fd(lfe))
    return SM_CODE_TUNING_FAILED;
    
  lfe->lfe_locked  = 0;
  lfe->lfe_status  = 0;
  
  sdr = &lfe->sdr;
  memset(sdr, 0, sizeof(struct sdr_state_t));
  sdr->mmi = mmi;
  sdr_init(sdr);

  return 0;
}

int
rtlsdr_frontend_tune0
(rtlsdr_frontend_t *lfe, dab_ensemble_instance_t *mmi, uint32_t freq)
{
	int r = 0;
	dab_ensemble_t *lm = mmi->mmi_ensemble;
		  
	r = rtlsdr_frontend_clear(lfe, mmi);
	if (r) return r;

	if (freq != (uint32_t)-1)
		lfe->lfe_freq = freq;
	else
		freq = lm->mm_freq;

	if (tvhtrace_enabled()) {
//		char buf2[256];
//		dvb_mux_conf_str(&lm->lm_tuning, buf2, sizeof(buf2));
		tvhdebug(LS_RTLSDR, "tuner %s tuning freq %i", lm->mm_nicename, freq);
	}
	r = rtlsdr_set_sample_rate(lfe->dev, 2048000);
	if (r < 0)
		tvherror(LS_RTLSDR, "WARNING: Failed to set sample rate.\n");

	/*------------------------------------------------
	Setting gain
	-------------------------------------------------*/
	r = rtlsdr_set_tuner_gain_mode(lfe->dev, 1);
	if (r != 0)
		tvherror(LS_RTLSDR, "WARNING: Failed to set tuner gain.\n");

	r = rtlsdr_set_agc_mode(lfe->dev, 1);
	if (r != 0)
		tvherror(LS_RTLSDR, "WARNING: Failed to set tuner gain.\n");

	r = rtlsdr_set_tuner_gain(lfe->dev, 166);
	if (r != 0)
		tvherror(LS_RTLSDR, "WARNING: Failed to set tuner gain.\n");

	/* Set the frequency */
	r = rtlsdr_set_center_freq(lfe->dev, freq);
	if (r < 0)
		tvherror(LS_RTLSDR, "WARNING: Failed to set center freq.\n");
	
	return r;
}

int
rtlsdr_frontend_tune1
(rtlsdr_frontend_t *lfe, dab_ensemble_instance_t *mmi, uint32_t freq)
{
	int r = 0;
	char buf1[256];

	lfe->mi_display_name((dab_input_t*)lfe, buf1, sizeof(buf1));
	tvhdebug(LS_RTLSDR, "%s - starting %s", buf1, mmi->mmi_ensemble->mm_nicename);

	/* Tune */
	tvhtrace(LS_RTLSDR, "%s - tuning", buf1);

	r = rtlsdr_frontend_tune0(lfe, mmi, freq);

	/* Start monitor */
	if (!r) {
		lfe->lfe_monitor = mclk() + sec2mono(4);
		mtimer_arm_rel(&lfe->lfe_monitor_timer, rtlsdr_frontend_monitor, lfe, ms2mono(50));
		lfe->lfe_ready = 1;
	}

	lfe->lfe_in_setup = 0;

	return r;
}

/* **************************************************************************
* Creation/Config
* *************************************************************************/

static dab_network_t *
rtlsdr_frontend_wizard_network(rtlsdr_frontend_t *lfe)
{
	return (dab_network_t *)LIST_FIRST(&lfe->mi_networks);
}

static htsmsg_t *
rtlsdr_frontend_wizard_get(tvh_input_t *ti, const char *lang)
{
	dab_network_t *mn = rtlsdr_frontend_wizard_network((rtlsdr_frontend_t *)ti);
	return dab_network_wizard_get((dab_input_t *)ti, NULL, mn, lang);
}

static void
rtlsdr_frontend_wizard_set(tvh_input_t *ti, htsmsg_t *conf, const char *lang)
{
	rtlsdr_frontend_t *lfe = (rtlsdr_frontend_t *) ti;
	const char *ntype = htsmsg_get_str(conf, "dab_network_type");
	dab_network_t *mn;
	htsmsg_t *nlist;

	if (LIST_FIRST(&lfe->mi_ensemble_active))
		return;
	dab_network_wizard_create(ntype, &nlist, lang);
	mn = rtlsdr_frontend_wizard_network(lfe);
	if (ntype && (mn == NULL || mn->mn_wizard)) {
		dab_input_set_networks((dab_input_t *) lfe, nlist);
		htsmsg_destroy(nlist);
		if (rtlsdr_frontend_wizard_network(lfe))
			dab_input_set_enabled((dab_input_t *) lfe, 1);
		rtlsdr_adapter_changed(lfe->lfe_adapter);
	}
	else {
		htsmsg_destroy(nlist);
	}
}


rtlsdr_frontend_t *
rtlsdr_frontend_create
(htsmsg_t *conf, rtlsdr_adapter_t *la) {
	const idclass_t *idc;
	rtlsdr_frontend_t *lfe;
	char lname[256];
	const char *str, *uuid = NULL;

	/* Internal config ID */
	if (conf)
		conf = htsmsg_get_map(conf, "#");
	if (conf)
		uuid = htsmsg_get_str(conf, "uuid");

	/* Fudge configuration for old network entry */
	if (conf) {
		if (!htsmsg_get_list(conf, "networks") &&
			(str = htsmsg_get_str(conf, "network"))) {
			htsmsg_t *l = htsmsg_create_list();
			htsmsg_add_str(l, NULL, str);
			htsmsg_add_msg(conf, "networks", l);
		}
	}
	idc = &rtlsdr_frontend_dab_class;

	lfe = calloc(1, sizeof(rtlsdr_frontend_t));
	if (!lfe) {
		tvhtrace(LS_RTLSDR, "calloc failed!");
		return NULL;
	}
	lfe = (rtlsdr_frontend_t *) dab_input_create0((dab_input_t *)lfe, idc, uuid, conf);
	if (!lfe) {
		tvhtrace(LS_RTLSDR, "rtlsdr_frontend_create0 failed!");
		return NULL;
	}

	/* Callbacks */
	lfe->mi_get_weight = dab_input_get_weight;
	lfe->mi_get_priority = dab_input_get_priority;
	lfe->mi_get_grace = dab_input_get_grace;

	/* Default name */
	if (!lfe->mi_name) {
		snprintf(lname, sizeof(lname), "%s : %s", la->device_name, N_("DAB"));
		lfe->mi_name = strdup(lname);
	}

	/* Input callbacks */
	lfe->ti_wizard_get = rtlsdr_frontend_wizard_get;
	lfe->ti_wizard_set = rtlsdr_frontend_wizard_set;
	lfe->mi_is_enabled = rtlsdr_frontend_is_enabled;
	lfe->mi_warm_ensemble = rtlsdr_frontend_warm_ensemble;
	lfe->mi_start_ensemble = rtlsdr_frontend_start_ensemble;
	lfe->mi_stop_ensemble = rtlsdr_frontend_stop_ensemble;
	lfe->mi_open_service = rtlsdr_frontend_open_service;
	lfe->mi_close_service = rtlsdr_frontend_close_service;
	lfe->mi_network_list = rtlsdr_frontend_network_list;
//	lfe->mi_update_pids = mpegts_mux_update_pids;
	lfe->mi_create_ensemble_instance = rtlsdr_frontend_create_ensemble_instance;
	lfe->mi_enabled_updated = rtlsdr_frontend_enabled_updated;
	lfe->mi_empty_status = dab_input_empty_status;

	/* Adapter link */
	lfe->lfe_adapter = la;
	LIST_INSERT_HEAD(&la->la_frontends, lfe, lfe_link);

	/* Double check enabled */
	rtlsdr_frontend_enabled_updated((dab_input_t *)lfe);

	return lfe;
}

void
rtlsdr_frontend_save(rtlsdr_frontend_t *lfe, htsmsg_t *fe)
{
	char id[16];
	htsmsg_t *m = htsmsg_create_map();

	/* Save frontend */
	dab_input_save((dab_input_t*)lfe, m);
	htsmsg_add_uuid(m, "uuid", &lfe->ti_id.in_uuid);

	/* Add to list */
	snprintf(id, sizeof(id), "#");
	htsmsg_add_msg(fe, id, m);
}


void
rtlsdr_frontend_destroy(rtlsdr_frontend_t *lfe)
{
	/* Ensure we're stopped */
	dab_input_stop_all((dab_input_t*)lfe);

	/* Close FDs */
	if (lfe->dev != NULL)
		rtlsdr_close(lfe->dev);

	/* Remove from adapter */
	LIST_REMOVE(lfe, lfe_link);

	/* Finish */
	dab_input_delete((dab_input_t*)lfe, 0);
}
