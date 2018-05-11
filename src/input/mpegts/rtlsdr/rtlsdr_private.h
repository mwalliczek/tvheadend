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

#endif /* __TVH_RTLSDR_PRIVATE_H__ */


