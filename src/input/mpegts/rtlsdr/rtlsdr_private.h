#ifndef __TVH_RTLSDR_PRIVATE_H__
#define __TVH_RTLSDR_PRIVATE_H__


#include "input.h"

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

};

extern const idclass_t rtlsdr_adapter_class;
extern const idclass_t rtlsdr_frontend_dab_class;

void rtlsdr_init(void);

#endif /* __TVH_RTLSDR_PRIVATE_H__ */


