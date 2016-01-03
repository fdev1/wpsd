#ifndef __PROVIDER_H__
#define __PROVIDER_H__

#include "upp.h"

#define WPS_PROVIDER_SUCCESS		( 0)
#define WPS_PROVIDER_FAILURE		(-1)

struct wps_context
{
	void (*logger)(int level, const char *fmt, ...);
};

struct wps_location *provider_get_location(int address_lookup);

int provider_init(struct wps_context *context);

#endif
