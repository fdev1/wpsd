#ifndef __UPP_H__
#define __UPP_H__

#include <time.h>

#define UPP_PROVIDER_TYPE_UNKNOWN		(0)
#define UPP_PROVIDER_TYPE_WIFI			(1)
#define UPP_PROVIDER_TYPE_GPS				(2)

struct config_entry
{
	const char *name;
	const char *value;
	struct config_entry *next;
};

struct wps_context
{
	int status;
	struct config_entry *config;
	const char * (*get_config)(struct wps_context *context, const char *name);
	void (*logger)(int level, const char *fmt, ...);
};


struct wps_street_address
{
	char *street_number;
	char **address_lines;
	char *city;
	char *postal_code;
	char *state;
	char *country;
};

struct wps_location
{
	double latitude;
	double longitude;
	double altitude;
	unsigned int sources;
	double accuracy;
	double speed;
	double bearing;
	time_t timestamp;
	int type;
	struct wps_street_address *street_address;
};

#endif
