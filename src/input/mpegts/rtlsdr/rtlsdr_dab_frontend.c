#include "tvheadend.h"
#include "input.h"
#include "rtlsdr_private.h"

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
* Class definition
* *************************************************************************/

static void
rtlsdr_frontend_class_changed(idnode_t *in)
{
	rtlsdr_adapter_t *la = ((rtlsdr_frontend_t*)in)->lfe_adapter;
	rtlsdr_adapter_changed(la);
}



rtlsdr_frontend_t *
rtlsdr_frontend_create
(htsmsg_t *conf, rtlsdr_adapter_t *la) {
	const idclass_t *idc;
	char id[16], lname[256], buf[256];
	rtlsdr_frontend_t *lfe;
	htsmsg_t *scconf;
	const char *str, *uuid = NULL, *muuid = NULL;
	char id[16];
	ssize_t r;

	/* Internal config ID */
	snprintf(id, sizeof(id), "#");
	if (conf)
		conf = htsmsg_get_map(conf, id);
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
	lfe = (rtlsdr_frontend_t*)mpegts_input_create0((mpegts_input_t*)lfe, idc, uuid, conf);
	if (!lfe) return NULL;

	/* Sysfs path */
	snprintf(lname, sizeof(lname), "/sys/class/rtlsdr/rtlsdr%d.frontend", la->dev_index);
	r = readlink(lname, buf, sizeof(buf));
	if (r > 0) {
		if (r == sizeof(buf)) r--;
		buf[r] = '\0';
		if (strncmp(str = buf, "../../", 6) == 0) str += 6;
		lfe->lfe_sysfs = strdup(str);
	}
}