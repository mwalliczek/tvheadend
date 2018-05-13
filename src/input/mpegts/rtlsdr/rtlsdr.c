#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <rtl-sdr.h>
#include <unistd.h>
#include <openssl/sha.h>

#include "tvheadend.h"
#include "input.h"
#include "rtlsdr_private.h"
#include "queue.h"
#include "fsmonitor.h"
#include "settings.h"

static htsmsg_t *
rtlsdr_adapter_class_save(idnode_t *in, char *filename, size_t fsize)
{
	rtlsdr_adapter_t *la = (rtlsdr_adapter_t*)in;
	htsmsg_t *m, *l;
	rtlsdr_frontend_t *lfe;
	char ubuf[UUID_HEX_SIZE];

	m = htsmsg_create_map();
	idnode_save(&la->th_id, m);

	if (filename) {
		l = htsmsg_create_map();
		LIST_FOREACH(lfe, &la->la_frontends, lfe_link)
			rtlsdr_frontend_save(lfe, l);
		htsmsg_add_msg(m, "frontends", l);
		snprintf(filename, fsize, "input/rtlsdr/adapters/%s",
			idnode_uuid_as_str(&la->th_id, ubuf));
	}
	return m;
}

static void
rtlsdr_adapter_class_get_title
(idnode_t *in, const char *lang, char *dst, size_t dstsize)
{
	rtlsdr_adapter_t *la = (rtlsdr_adapter_t*)in;
	snprintf(dst, dstsize, "%s", la->device_name ? : la->product);
}

static const void *
rtlsdr_adapter_class_active_get(void *obj)
{
	static int active;
	rtlsdr_adapter_t *la = (rtlsdr_adapter_t*)obj;
	active = la->la_is_enabled(la);
	return &active;
}

static idnode_set_t *
rtlsdr_adapter_class_get_childs(idnode_t *in)
{
//	rtlsdr_adapter_t *la = (rtlsdr_adapter_t*)in;
	idnode_set_t *is = idnode_set_create(0);
	return is;
}

const idclass_t rtlsdr_adapter_class =
{
	.ic_class = "rtlsdr_adapter",
	.ic_caption = N_("rtlsdr adapter"),
	.ic_event = "rtlsdr_adapter",
	.ic_save = rtlsdr_adapter_class_save,
	.ic_get_childs = rtlsdr_adapter_class_get_childs,
	.ic_get_title = rtlsdr_adapter_class_get_title,
	.ic_properties = (const property_t[]) {
		{
			.type = PT_BOOL,
				.id = "active",
				.name = N_("Active"),
				.opts = PO_RDONLY | PO_NOSAVE | PO_NOUI,
				.get = rtlsdr_adapter_class_active_get,
		},
	{
		.type = PT_INT,
		.id = "dev_index",
		.name = N_("Device index"),
		.desc = N_("Index of the device."),
		.opts = PO_RDONLY,
		.off = offsetof(rtlsdr_adapter_t, dev_index),
	},
			{}
}
};

/*
* Check if enabled
*/
static int
rtlsdr_adapter_is_enabled(rtlsdr_adapter_t *la)
{
	rtlsdr_frontend_t *lfe;
	LIST_FOREACH(lfe, &la->la_frontends, lfe_link) {
		if (lfe->mi_is_enabled((mpegts_input_t*)lfe, NULL, 0, -1) != MI_IS_ENABLED_NEVER)
			return 1;
	}
	return 0;
}


/*
* Create
*/
static rtlsdr_adapter_t *
rtlsdr_adapter_create
(const char *uuid, htsmsg_t *conf,
	int number, char *vendor, char *product, char *serial, const char *device_name)
{
	rtlsdr_adapter_t *la;

	/* Create */
	la = calloc(1, sizeof(rtlsdr_adapter_t));
	if (!tvh_hardware_create0((tvh_hardware_t*)la, &rtlsdr_adapter_class,
		uuid, conf)) {
		/* Note: la variable is freed in tvh_hardware_create0() */
		return NULL;
	}

	/* Setup */
	la->dev_index = number;
	la->vendor = strdup(vendor);
	la->product = strdup(product);
	la->serial = strdup(serial);
	la->device_name = strdup(device_name);
	/* Callbacks */
	la->la_is_enabled = rtlsdr_adapter_is_enabled;

	return la;
}

/*
*
*/
static rtlsdr_adapter_t *
rtlsdr_adapter_new(int number, char *vendor, char *product, char *serial, const char *device_name, htsmsg_t **conf, int *save)
{
	rtlsdr_adapter_t *la;
	SHA_CTX sha1;
	uint8_t uuidbin[20];
	char uhex[UUID_HEX_SIZE];

	/* Create hash for adapter */
	SHA1_Init(&sha1);
	SHA1_Update(&sha1, &number, sizeof(number));
	SHA1_Final(uuidbin, &sha1);

	bin2hex(uhex, sizeof(uhex), uuidbin, sizeof(uuidbin));

	/* Load config */
	*conf = hts_settings_load("input/rtlsdr/adapters/%s", uhex);
	if (*conf == NULL)
		*save = 1;

	/* Create */
	if (!(la = rtlsdr_adapter_create(uhex, *conf, number, vendor, product, serial, device_name))) {
		htsmsg_destroy(*conf);
		*conf = NULL;
		return NULL;
	}

	tvhinfo(LS_RTLSDR, "adapter added %d", number);
	return la;
}

static void
rtlsdr_adapter_add(int device_number)
{
	rtlsdr_adapter_t *la = NULL;
	htsmsg_t *conf = NULL, *feconf = NULL;
	int save = 0;
	char vendor[256], product[256], serial[256];
	const char *device_name;

	tvhtrace(LS_RTLSDR, "scanning adapter %d", device_number);
	rtlsdr_get_device_usb_strings(device_number, vendor, product, serial);
	device_name = rtlsdr_get_device_name(device_number);
	tvhinfo(LS_RTLSDR, "  %d:  %s, %s, SN: %s, %s", device_number, vendor, product, serial, device_name);
	la = rtlsdr_adapter_new(device_number, vendor, product, serial, device_name, &conf, &save);
	if (la == NULL) {
		tvherror(LS_RTLSDR, "failed to create %d", device_number);
		return; // Note: save to return here as global_lock is held
	}
	feconf = htsmsg_get_map(conf, "frontends");
	rtlsdr_frontend_create(feconf, la);
	if (conf)
		htsmsg_destroy(conf);
	/* Save configuration */
	if (save && la)
		rtlsdr_adapter_changed(la);
}

void rtlsdr_init() {
	int i, device_count;
	idclass_register(&rtlsdr_adapter_class);
	idclass_register(&rtlsdr_frontend_dab_class);
	/*---------------------------------------------------
	Looking for device and open connection
	----------------------------------------------------*/
	device_count = rtlsdr_get_device_count();
	if (!device_count) {
		tvhinfo(LS_RTLSDR, "No supported devices found.");
		exit(1);
	}

	tvhinfo(LS_RTLSDR, "Found %d device(s):", device_count);
	for (i = 0; i < device_count; i++) {
		rtlsdr_adapter_add(i);
	}
}

static void
rtlsdr_adapter_del(int number)
{
	rtlsdr_frontend_t *lfe, *lfe_next;
	rtlsdr_adapter_t *la = NULL;
	tvh_hardware_t *th;

	LIST_FOREACH(th, &tvh_hardware, th_link)
		if (idnode_is_instance(&th->th_id, &rtlsdr_adapter_class)) {
			la = (rtlsdr_adapter_t*)th;
			if (number == la->dev_index)
				break;
		}
	if (!th) return;

	idnode_save_check(&la->th_id, 0);

	/* Delete the frontends */
	for (lfe = LIST_FIRST(&la->la_frontends); lfe != NULL; lfe = lfe_next) {
		lfe_next = LIST_NEXT(lfe, lfe_link);
		rtlsdr_frontend_destroy(lfe);
	}
	/* Free memory */
	free(la->device_name);
	free(la->product);
	free(la->serial);
	free(la->vendor);

	/* Delete */
	tvh_hardware_delete((tvh_hardware_t*)la);

	free(la);
}

void
rtlsdr_done(void)
{
	rtlsdr_adapter_t *la;
	tvh_hardware_t *th, *n;

	for (th = LIST_FIRST(&tvh_hardware); th != NULL; th = n) {
		n = LIST_NEXT(th, th_link);
		if (idnode_is_instance(&th->th_id, &rtlsdr_adapter_class)) {
			la = (rtlsdr_adapter_t*)th;
			rtlsdr_adapter_del(la->dev_index);
		}
	}
}
