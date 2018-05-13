#include "tvheadend.h"
#include "input.h"
#include "rtlsdr_private.h"

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
	.ic_caption = N_("Linux DVB frontend"),
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
	return 5;
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
		tvhtrace(LS_RTLSDR, "%s - waiting for dvr thread", buf1);
		pthread_join(lfe->lfe_dvr_thread, NULL);
		tvh_pipe_close(&lfe->lfe_dvr_pipe);
		tvhdebug(LS_RTLSDR, "%s - stopped dvr thread", buf1);
	}

	/* Not locked */
	lfe->lfe_ready = 0;

	lfe->lfe_refcount--;
	lfe->lfe_in_setup = 0;
	lfe->lfe_freq = 0;
	mpegts_pid_done(&lfe->lfe_pids);
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

static void
rtlsdr_frontend_update_pids
(mpegts_input_t *mi, mpegts_mux_t *mm)
{
	rtlsdr_frontend_t *lfe = (rtlsdr_frontend_t*)mi;
	mpegts_pid_t *mp;
	mpegts_pid_sub_t *mps;

	mpegts_pid_done(&lfe->lfe_pids);
	RB_FOREACH(mp, &mm->mm_pids, mp_link) {
		if (mp->mp_pid == MPEGTS_FULLMUX_PID)
			lfe->lfe_pids.all = 1;
		else if (mp->mp_pid < MPEGTS_FULLMUX_PID) {
			RB_FOREACH(mps, &mp->mp_subs, mps_link)
				mpegts_pid_add(&lfe->lfe_pids, mp->mp_pid, mps->mps_weight);
		}
	}

	if (lfe->lfe_dvr_pipe.wr > 0)
		tvh_write(lfe->lfe_dvr_pipe.wr, "c", 1);
}

static idnode_set_t *
rtlsdr_frontend_network_list(mpegts_input_t *mi)
{
	tvhtrace(LS_RTLSDR, "%s: network list for DAB",
		mi->mi_name ? : "");

	return dvb_network_list_by_fe_type(DVB_TYPE_DAB);
}

int
rtlsdr_frontend_tune
(rtlsdr_frontend_t *lfe, mpegts_mux_instance_t *mmi, uint32_t freq)
{
	int r = 0;
	char buf1[256];

	lfe->mi_display_name((mpegts_input_t*)lfe, buf1, sizeof(buf1));
	tvhdebug(LS_RTLSDR, "%s - starting %s", buf1, mmi->mmi_mux->mm_nicename);

	/* Tune */
	tvhtrace(LS_RTLSDR, "%s - tuning", buf1);



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
		lfe->mi_name = strdup(N_("DAB"));
	}

	/* Input callbacks */
	lfe->ti_wizard_get = rtlsdr_frontend_wizard_get;
	lfe->ti_wizard_set = rtlsdr_frontend_wizard_set;
	lfe->mi_is_enabled = rtlsdr_frontend_is_enabled;
	lfe->mi_warm_mux = rtlsdr_frontend_warm_mux;
	lfe->mi_start_mux = rtlsdr_frontend_start_mux;
	lfe->mi_stop_mux = rtlsdr_frontend_stop_mux;
	lfe->mi_network_list = rtlsdr_frontend_network_list;
	lfe->mi_update_pids = rtlsdr_frontend_update_pids;
	lfe->mi_enabled_updated = rtlsdr_frontend_enabled_updated;
	lfe->mi_empty_status = mpegts_input_empty_status;

	/* Adapter link */
	lfe->lfe_adapter = la;
	LIST_INSERT_HEAD(&la->la_frontends, lfe, lfe_link);

	tvh_cond_init(&lfe->lfe_dvr_cond);
	mpegts_pid_init(&lfe->lfe_pids);

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
