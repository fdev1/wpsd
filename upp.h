#ifndef __UPP_H__
#define __UPP_H__

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

#endif
