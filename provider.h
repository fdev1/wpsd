#ifndef __PROVIDER_H__
#define __PROVIDER_H__

#define WPS_PROVIDER_SUCCESS		( 0)
#define WPS_PROVIDER_FAILURE		(-1)

struct wps_context
{
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
	unsigned int sources;
	double accuracy;
	double speed;
	double bearing;
	struct wps_street_address *street_address;
};

struct wps_location *provider_get_location(int address_lookup);

int provider_init(struct wps_context *context);

#endif
