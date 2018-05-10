#include "input.h"

typedef struct rtlsdrdab_hardware    rtlsdrdab_hardware_t;
typedef struct rtlsdrdab_adapter     rtlsdrdab_adapter_t;
typedef struct rtlsdrdab_frontend    rtlsdrdab_frontend_t;

struct rtlsdrdab_adapter
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
	LIST_HEAD(, rtlsdrdab_frontend) la_frontends;

	/*
	* Functions
	*/
	int(*la_is_enabled) (rtlsdrdab_adapter_t *la);
};


