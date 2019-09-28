#include <fcntl.h>
#include <math.h>

#include "tvheadend.h"
#include "input.h"
#include "rtlsdr_private.h"
#include "phasereference.h"
#include "ofdmDecoder.h"
#include "ficHandler.h"

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
	.ic_super = &mpegts_input_class,
	.ic_class = "rtlsdr_frontend",
	.ic_caption = N_("RTL SDR frontend"),
	.ic_doc = tvh_doc_rtlsdr_frontend_class,
	.ic_changed = rtlsdr_frontend_class_changed,
	.ic_properties = (const property_t[]) {
		{}
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
rtlsdr_frontend_close_fd(rtlsdr_frontend_t *lfe, int dev_index)
{
	if (lfe->dev == NULL)
		return;

	tvhtrace(LS_RTLSDR, "closing FE %d", dev_index);
	rtlsdr_close(lfe->dev);
	lfe->dev = NULL;
}

static int
rtlsdr_frontend_open_fd(rtlsdr_frontend_t *lfe, int dev_index)
{
	int r;

	if (lfe->dev != NULL)
		return 0;

	if (lfe->dev == NULL) {
		r = rtlsdr_open(&lfe->dev, dev_index);
		if (r < 0) {
			tvherror(LS_RTLSDR, "Failed to open rtlsdr device #%d.\n", dev_index);
			exit(1);
		}
	}

	tvhtrace(LS_RTLSDR, "opening FE %d", dev_index);

	return lfe->dev == NULL;
}

static void
rtlsdr_frontend_enabled_updated(mpegts_input_t *mi)
{
	int dev_index;
	rtlsdr_frontend_t *lfe = (rtlsdr_frontend_t*)mi;

	dev_index = lfe->lfe_adapter->dev_index;

	/* Ensure disabled */
	if (!mi->mi_enabled) {
		tvhtrace(LS_RTLSDR, "%d - disabling tuner", dev_index);
		rtlsdr_frontend_close_fd(lfe, dev_index);
	}
	else if (lfe->dev == NULL) {

		rtlsdr_frontend_open_fd(lfe, dev_index);

	}
}

static int
rtlsdr_frontend_get_grace(mpegts_input_t *mi, mpegts_mux_t *mm)
{
	return 60;
}

static int
rtlsdr_frontend_is_enabled
(mpegts_input_t *mi, mpegts_mux_t *mm, int flags, int weight)
{
	rtlsdr_frontend_t *lfe = (rtlsdr_frontend_t*)mi;
	int w;

	if (lfe->lfe_adapter == NULL)
		return MI_IS_ENABLED_NEVER;
	w = mpegts_input_is_enabled(mi, mm, flags, weight);
	if (w != MI_IS_ENABLED_OK)
		return w;
	if (mm == NULL)
		return MI_IS_ENABLED_OK;
	if (lfe->lfe_in_setup)
		return MI_IS_ENABLED_RETRY;
	return MI_IS_ENABLED_OK;
}

static void
rtlsdr_frontend_stop_mux
(mpegts_input_t *mi, mpegts_mux_instance_t *mmi)
{
	char buf1[256];
	rtlsdr_frontend_t *lfe = (rtlsdr_frontend_t*)mi;

	mi->mi_display_name(mi, buf1, sizeof(buf1));
	tvhdebug(LS_RTLSDR, "%s - stopping %s", buf1, mmi->mmi_mux->mm_nicename);

	/* Stop thread */
	if (lfe->lfe_dvr_pipe.wr > 0) {
		tvh_write(lfe->lfe_dvr_pipe.wr, "", 1);
		lfe->running = 0;
		tvhtrace(LS_RTLSDR, "%s - waiting for dvr thread", buf1);
		pthread_join(lfe->demod_thread, NULL);
		tvh_pipe_close(&lfe->lfe_dvr_pipe);
		tvhdebug(LS_RTLSDR, "%s - stopped dvr thread", buf1);
		rtlsdr_cancel_async(lfe->dev);
	}

	destroyFicHandler(&lfe->sdr);
	destroyPhaseReference(&lfe->sdr);
	destroyOfdmDecoder(&lfe->sdr);
	cbFree(&lfe->sdr.fifo);

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
rtlsdr_frontend_warm_mux(mpegts_input_t *mi, mpegts_mux_instance_t *mmi)
{
	rtlsdr_frontend_t *lfe = (rtlsdr_frontend_t*)mi, *lfe2 = NULL;
	mpegts_mux_instance_t *lmmi = NULL;
	int r;

	r = mpegts_input_warm_mux(mi, mmi);
	if (r)
		return r;

	/* Stop other active frontend (should be only one) */
	LIST_FOREACH(lfe2, &lfe->lfe_adapter->la_frontends, lfe_link) {
		if (lfe2 == lfe) continue;
		lmmi = LIST_FIRST(&lfe2->mi_mux_active);
		if (lmmi) {
			/* Stop */
			lmmi->mmi_mux->mm_stop(lmmi->mmi_mux, 1, SM_CODE_ABORTED);
		}
		rtlsdr_frontend_close_fd(lfe2, lfe->lfe_adapter->dev_index);
	}
	return 0;
}

static int
rtlsdr_frontend_start_mux
(mpegts_input_t *mi, mpegts_mux_instance_t *mmi, int weight)
{
	rtlsdr_frontend_t *lfe = (rtlsdr_frontend_t*)mi;
	int res;

	assert(lfe->lfe_in_setup == 0);

	lfe->lfe_refcount++;
	lfe->lfe_in_setup = 1;

	res = rtlsdr_frontend_tune((rtlsdr_frontend_t*)mi, mmi, -1);

	if (res) {
		lfe->lfe_in_setup = 0;
		lfe->lfe_refcount--;
	}
	return res;
}

static idnode_set_t *
rtlsdr_frontend_network_list(mpegts_input_t *mi)
{
	tvhtrace(LS_RTLSDR, "%s: network list for DAB",
		mi->mi_name ? : "");

	return dvb_network_list_by_fe_type(DVB_TYPE_DAB);
}

static void rtlsdr_dab_callback(uint8_t *buf, uint32_t len, void *ctx)
{
	rtlsdr_frontend_t *lfe = ctx;
	struct sdr_state_t *sdr = &lfe->sdr;
	tvhtrace(LS_RTLSDR, "callback with %u bytes, count %u", len, sdr->fifo.count);
	if (!ctx) {
		return;
	}
	if (lfe->lfe_dvr_pipe.wr <= 0 || !lfe->running) {
		return;
	}
	/* write input data into fifo */
	cbWrite(&(sdr->fifo), buf, len);
	tvh_write(lfe->lfe_control_pipe.wr, "", 1);
}

static void *rtlsdr_demod_thread_fn(void *arg)
{
	rtlsdr_frontend_t *lfe = arg;
	struct sdr_state_t *sdr = &lfe->sdr;
	float		fineCorrector = 0;
	float		coarseCorrector = 0;
	float _Complex v[T_F / 2];
	int32_t		startIndex;
	int i;
	float _Complex	FreqCorr;
	int32_t		syncBufferIndex = 0;
	float		cLevel = 0;
	float _Complex sample;
	int32_t		counter;
	int32_t		syncBufferSize = 32768;
	int32_t		syncBufferMask = syncBufferSize - 1;
	float		envBuffer[syncBufferSize];
	float _Complex ofdmBuffer[T_s];
	int		ofdmSymbolCount = 0;
	int		dip_attempts = 0;
	int		index_attempts = 0;

	tvhtrace(LS_RTLSDR, "start polling");
	/* Read */
	if (getSamples(lfe, v, T_F / 2, 0) < T_F / 2) {
		tvherror(LS_RTLSDR, "getSamples failed");
		return 0;
	}
	tvhtrace(LS_RTLSDR, "started, sLevel: %.6f", sdr->sLevel);
	while (tvheadend_is_running() && lfe->lfe_dvr_pipe.rd > 0 && lfe->running) {
		//      As long as we are not (time) synced, we try to handle that
		if (!sdr->isSynced) {
			syncBufferIndex = 0;
			cLevel = 0;
			for (i = 0; i < 50; i++) {
				if (getSample(lfe, &sample, &envBuffer[syncBufferIndex], 0) == 0) {
					tvherror(LS_RTLSDR, "getSamples failed");
					return 0;
				}
				cLevel += envBuffer[syncBufferIndex];
				syncBufferIndex++;
			}
			tvhtrace(LS_RTLSDR, "trying to find a sync, sLevel: %.6f, cLevel: %.6f", sdr->sLevel, cLevel);

			//	We now have initial values for cLevel (i.e. the sum
			//	over the last 50 samples) and sLevel, the long term average.
			//	here we start looking for the null level, i.e. a dip
			counter = 0;
			while (cLevel / 50 > 0.50 * sdr->sLevel) {
				if (getSample(lfe, &sample, &envBuffer[syncBufferIndex], coarseCorrector + fineCorrector) == 0) {
					tvherror(LS_RTLSDR, "getSamples failed");
					return 0;
				}
				cLevel += envBuffer[syncBufferIndex] -
					envBuffer[(syncBufferIndex - 50) & syncBufferMask];
				syncBufferIndex = (syncBufferIndex + 1) & syncBufferMask;
				counter++;

				if (counter > 2 * T_F) { // hopeless	
					break;
				}
			}
			//	if we have 5 successive attempts are failing, signal our bosses
			if (counter > 2 * T_F) {
				tvhtrace(LS_RTLSDR, "trying to find a dip failed");
				if (++dip_attempts > 10) {
					dip_attempts = 0;
					//	               syncsignalHandler (false, userData);
				}
				continue;
			}
			tvhtrace(LS_RTLSDR, "found begin of null period");
			//	It seemed we found a dip that started app 65/100 * 50 samples earlier.
			//	We now start looking for the end of the null period.

			counter = 0;
			while (cLevel / 50 < 0.75 * sdr->sLevel) {
				if (getSample(lfe, &sample, &envBuffer[syncBufferIndex], coarseCorrector + fineCorrector) == 0) {
					tvherror(LS_RTLSDR, "getSamples failed");
					return 0;
				}
				cLevel += envBuffer[syncBufferIndex] -
					envBuffer[(syncBufferIndex - 50) & syncBufferMask];
				syncBufferIndex = (syncBufferIndex + 1) & syncBufferMask;
				counter++;
				if (counter > T_null + 50)  // hopeless
					break;
			}
			if (counter > T_null + 50) {
				tvhtrace(LS_RTLSDR, "found end of null period failed");
				continue;
			}
			tvhtrace(LS_RTLSDR, "found end of null period");
			dip_attempts = 0;
		}
		//      We arrive here when time synchronized, either from above
		//      or after having processed a frame
		//      We now have to find the exact first sample of the non-null period.
		//      We use a correlation that will find the first sample after the
		//      cyclic prefix.
		//      Now read in Tu samples. The precise number is not really important
		//      as long as we can be sure that the first sample to be identified
		//      is part of the samples read.
		if (getSamples(lfe, ofdmBuffer, T_u, coarseCorrector + fineCorrector) < T_u) {
			tvherror(LS_RTLSDR, "getSamples failed");
			return 0;
		}
		startIndex = phaseReferenceFindIndex(sdr, ofdmBuffer);
		if (startIndex < 0) { // no sync, try again
			sdr->isSynced = 0;
			if (++index_attempts > 10) {
				tvhtrace(LS_RTLSDR, "sync failed");
			}
			continue;
		}
#ifdef TRACE_PHASEREFERENCE
        	FILE *pFile = fopen ("/tmp/phasereferenceInput", "wb");
        	fwrite (ofdmBuffer, sizeof(float _Complex), T_s, pFile);
        	fclose (pFile);
        	exit(0);
#endif		
		index_attempts = 0;
		tvhtrace(LS_RTLSDR, "sync found!");
		sdr->isSynced = 1;
		//	Once here, we are synchronized, we need to copy the data we
		//	used for synchronization for block 0

		memmove(ofdmBuffer, &ofdmBuffer[startIndex],
			(T_u - startIndex) * sizeof(float _Complex));
		int ofdmBufferIndex = T_u - startIndex;

		//	Block 0 is special in that it is used for coarse time synchronization
		//	and its content is used as a reference for decoding the
		//	first datablock.
		//	We read the missing samples in the ofdm buffer
		if (getSamples(lfe, &ofdmBuffer[ofdmBufferIndex],
			T_u - ofdmBufferIndex, coarseCorrector + fineCorrector) < T_u - ofdmBufferIndex) {
			tvherror(LS_RTLSDR, "getSamples failed");
			return 0;
		}
		processBlock_0(sdr, ofdmBuffer);
		tvhtrace(LS_RTLSDR, "snr: %.6f", sdr->mmi->tii_stats.snr / 10000.0);

		//
		//	Here we look only at the block_0 when we need a coarse
		//	frequency synchronization.
		//	The width is limited to 2 * 35 Khz (i.e. positive and negative)
		if (!sdr->fibProcessorIsSynced) {
			int correction = phaseReferenceEstimateOffset(sdr, ofdmBuffer);
			if (correction != 100) {
				coarseCorrector += correction * carrierDiff;
				if (fabsf(coarseCorrector) > Khz(35))
					coarseCorrector = 0;
			}
		}
		//
		//	after block 0, we will just read in the other (params -> L - 1) blocks
		//	The first ones are the FIC blocks. We immediately
		//	start with building up an average of the phase difference
		//	between the samples in the cyclic prefix and the
		//	corresponding samples in the datapart.
		///	and similar for the (params. L - 4) MSC blocks
		FreqCorr = 0.0 + 0.0 * I;
		for (ofdmSymbolCount = 1;
			ofdmSymbolCount < (uint16_t)L; ofdmSymbolCount++) {
			if (getSamples(lfe, ofdmBuffer, T_s, coarseCorrector + fineCorrector) < T_s) {
				tvherror(LS_RTLSDR, "getSamples failed");
				return 0;
			}
			for (i = (int)T_u; i < (int)T_s; i++) {
				FreqCorr += ofdmBuffer[i] * conjf(ofdmBuffer[i - T_u]);
			}

			decodeBlock(sdr, ofdmBuffer, ofdmSymbolCount);
		}

		//	we integrate the newly found frequency error with the
		//	existing frequency error.
		fineCorrector += 0.1 * cargf(FreqCorr) / M_PI * carrierDiff;

		//	at the end of the frame, just skip Tnull samples
		if (getSamples(lfe, ofdmBuffer, T_null, coarseCorrector + fineCorrector) < T_null) {
			tvherror(LS_RTLSDR, "getSamples failed");
			return 0;
		}
		if (fineCorrector > carrierDiff / 2) {
			coarseCorrector += carrierDiff;
			fineCorrector -= carrierDiff;
		}
		else
			if (fineCorrector < -carrierDiff / 2) {
				coarseCorrector -= carrierDiff;
				fineCorrector += carrierDiff;
			}
	}
	return 0;
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
	mpegts_mux_instance_t *mmi = LIST_FIRST(&lfe->mi_mux_active);
//	mpegts_mux_t *mm;
//	service_t *s;
	uint32_t period = MINMAX(lfe->lfe_status_period, 250, 8000);
	struct sdr_state_t *sdr;
	signal_state_t status;
	signal_status_t sigstat;
	streaming_message_t sm;
	service_t *s;

	lfe->mi_display_name((mpegts_input_t*)lfe, buf, sizeof(buf));
	tvhtrace(LS_RTLSDR, "%s - checking FE status%s", buf, lfe->lfe_ready ? " (ready)" : "");

	/* Disabled */
	if (!lfe->mi_enabled && mmi)
		mmi->mmi_mux->mm_stop(mmi->mmi_mux, 1, SM_CODE_ABORTED);

	/* Close FE */
	if (lfe->dev != NULL && !lfe->lfe_refcount) {
		rtlsdr_frontend_close_fd(lfe, lfe->lfe_adapter->dev_index);
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
		sdr = &lfe->sdr;
		memset(sdr, 0, sizeof(struct sdr_state_t));
		sdr->mmi = mmi;
		sdr_init(sdr);
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
		lfe->lfe_locked = lfe->sdr.fibProcessorIsSynced;
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

		LIST_FOREACH(s, &mmi->mmi_mux->mm_transports, s_active_link) {
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
rtlsdr_frontend_tune
(rtlsdr_frontend_t *lfe, mpegts_mux_instance_t *mmi, uint32_t freq)
{
	int r = 0;
	char buf1[256];
	dvb_mux_t *lm = (dvb_mux_t*)mmi->mmi_mux;
	dvb_mux_conf_t *dmc;

	lfe->mi_display_name((mpegts_input_t*)lfe, buf1, sizeof(buf1));
	tvhdebug(LS_RTLSDR, "%s - starting %s", buf1, mmi->mmi_mux->mm_nicename);

	/* Tune */
	tvhtrace(LS_RTLSDR, "%s - tuning", buf1);

	if (tvhtrace_enabled()) {
		char buf2[256];
		dvb_mux_conf_str(&lm->lm_tuning, buf2, sizeof(buf2));
		tvhtrace(LS_RTLSDR, "tuner %s tuning to %s (freq %i)", buf1, buf2, freq);
	}
	dmc = &lm->lm_tuning;
	if (freq != (uint32_t)-1)
		lfe->lfe_freq = freq;
	else
		freq = lfe->lfe_freq = dmc->dmc_fe_freq;

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
	else {
		time(&lfe->lfe_monitor);
		lfe->lfe_monitor += 4;
		mtimer_arm_rel(&lfe->lfe_monitor_timer, rtlsdr_frontend_monitor, lfe, ms2mono(50));
		lfe->lfe_ready = 1;
	}

	lfe->lfe_in_setup = 0;

	return r;
}

/* **************************************************************************
* Creation/Config
* *************************************************************************/

static mpegts_network_t *
rtlsdr_frontend_wizard_network(rtlsdr_frontend_t *lfe)
{
	return (mpegts_network_t *)LIST_FIRST(&lfe->mi_networks);
}

static htsmsg_t *
rtlsdr_frontend_wizard_get(tvh_input_t *ti, const char *lang)
{
	rtlsdr_frontend_t *lfe = (rtlsdr_frontend_t*)ti;
	mpegts_network_t *mn;
	const idclass_t *idc = NULL;

	mn = rtlsdr_frontend_wizard_network(lfe);
	idc = dvb_network_class_by_fe_type(DVB_TYPE_DAB);
	return mpegts_network_wizard_get((mpegts_input_t *)lfe, idc, mn, lang);
}

static void
rtlsdr_frontend_wizard_set(tvh_input_t *ti, htsmsg_t *conf, const char *lang)
{
	rtlsdr_frontend_t *lfe = (rtlsdr_frontend_t*)ti;
	const char *ntype = htsmsg_get_str(conf, "mpegts_network_type");
	mpegts_network_t *mn;
	htsmsg_t *nlist;

	if (LIST_FIRST(&lfe->mi_mux_active))
		return;
	mpegts_network_wizard_create(ntype, &nlist, lang);
	mn = rtlsdr_frontend_wizard_network(lfe);
	if (ntype && (mn == NULL || mn->mn_wizard)) {
		mpegts_input_set_networks((mpegts_input_t *)lfe, nlist);
		htsmsg_destroy(nlist);
		if (rtlsdr_frontend_wizard_network(lfe))
			mpegts_input_set_enabled((mpegts_input_t *)lfe, 1);
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
	lfe = (rtlsdr_frontend_t*)mpegts_input_create0((mpegts_input_t*)lfe, idc, uuid, conf);
	if (!lfe) {
		tvhtrace(LS_RTLSDR, "mpegts_input_create0 failed!");
		return NULL;
	}

	/* Callbacks */
	lfe->mi_get_weight = mpegts_input_get_weight;
	lfe->mi_get_priority = mpegts_input_get_priority;
	lfe->mi_get_grace = rtlsdr_frontend_get_grace;

	/* Default name */
	if (!lfe->mi_name) {
		snprintf(lname, sizeof(lname), "%s : %s", la->device_name, N_("DAB"));
		lfe->mi_name = strdup(lname);
	}

	/* Input callbacks */
	lfe->ti_wizard_get = rtlsdr_frontend_wizard_get;
	lfe->ti_wizard_set = rtlsdr_frontend_wizard_set;
	lfe->mi_is_enabled = rtlsdr_frontend_is_enabled;
	lfe->mi_warm_mux = rtlsdr_frontend_warm_mux;
	lfe->mi_start_mux = rtlsdr_frontend_start_mux;
	lfe->mi_stop_mux = rtlsdr_frontend_stop_mux;
	lfe->mi_network_list = rtlsdr_frontend_network_list;
//	lfe->mi_update_pids = mpegts_mux_update_pids;
	lfe->mi_enabled_updated = rtlsdr_frontend_enabled_updated;
	lfe->mi_empty_status = mpegts_input_empty_status;

	/* Adapter link */
	lfe->lfe_adapter = la;
	LIST_INSERT_HEAD(&la->la_frontends, lfe, lfe_link);

	/* Double check enabled */
	rtlsdr_frontend_enabled_updated((mpegts_input_t*)lfe);

	return lfe;
}

void
rtlsdr_frontend_save(rtlsdr_frontend_t *lfe, htsmsg_t *fe)
{
	char id[16];
	htsmsg_t *m = htsmsg_create_map();

	/* Save frontend */
	mpegts_input_save((mpegts_input_t*)lfe, m);
	htsmsg_add_uuid(m, "uuid", &lfe->ti_id.in_uuid);

	/* Add to list */
	snprintf(id, sizeof(id), "#");
	htsmsg_add_msg(fe, id, m);
}


void
rtlsdr_frontend_destroy(rtlsdr_frontend_t *lfe)
{
	/* Ensure we're stopped */
	mpegts_input_stop_all((mpegts_input_t*)lfe);

	/* Close FDs */
	if (lfe->dev != NULL)
		rtlsdr_close(lfe->dev);

	/* Remove from adapter */
	LIST_REMOVE(lfe, lfe_link);

	/* Finish */
	mpegts_input_delete((mpegts_input_t*)lfe, 0);
}
