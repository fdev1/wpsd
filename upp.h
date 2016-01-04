#ifndef __UPP_H__
#define __UPP_H__

#include <time.h>

#define UPP_PROVIDER_TYPE_UNKNOWN		(0)
#define UPP_PROVIDER_TYPE_WIFI			(1)
#define UPP_PROVIDER_TYPE_GPS				(2)

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
