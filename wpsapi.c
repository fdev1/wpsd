#include <stdlib.h>
#include <unistd.h>
#include <dlfcn.h>
#include "wpsapi/include/wpsapi.h"
#include "logger.h"
#include "upp_provider.h"

#define WPSAPI_LIB "/usr/lib/uppd/providers/lib/libwpsapi-ubuntu.so"
#define API_KEY "eJwz5DQ0AAFTA2NjzmoLcwtnVxNXF10zS0sLXVMLMzddZyNnN11zNwsXR0tjoICjUy0AFI4LWw"

/*
 * Import libwpsapi.so functions
 */
typedef WPS_ReturnCode (*_wps_set_key_func)(const char* key);
typedef WPS_ReturnCode (*_wps_load_func)();
typedef WPS_ReturnCode (*_wps_location_func)(WPS_SimpleAuthentication* authentication,
	WPS_StreetAddressLookup street_address_lookup, WPS_Location** location);
typedef void (*_wps_free_location_func)(WPS_Location*);
static void *wpsapi_lib = NULL;
static _wps_set_key_func _wps_set_key = NULL;
static _wps_load_func _wps_load = NULL;
static _wps_location_func _wps_location = NULL;
static _wps_free_location_func _wps_free_location = NULL;
static char *_wpsapi_lib_path = NULL;
static struct wps_context *_context = NULL;

struct wps_location *provider_get_location(int address_lookup)
{
	WPS_ReturnCode ret;
	WPS_Location *location;
	WPS_StreetAddressLookup lookup =
		(address_lookup) ? WPS_FULL_STREET_ADDRESS_LOOKUP : WPS_NO_STREET_ADDRESS_LOOKUP;
	struct wps_location *result;
	result = malloc(sizeof(struct wps_location));
	if (result == NULL)
	{
		_context->logger(LOG_ERR, "Out of memory: malloc() failed");
		return result;
	}
	ret = _wps_location(NULL, lookup, &location);
	if (ret != WPS_OK)
	{
		_context->logger(LOG_ERR, "Call to WPS_location() failed");
		return NULL;
	}
	result->latitude = location->latitude;
	result->longitude = location->longitude;
	result->sources = location->nap;
	result->accuracy = location->hpe;
	result->speed = location->speed;
	result->bearing = location->bearing;
	#if 0
	if (_address_lookup)
	{
		i += sprintf(_location + i, "Street Number: %s\n", 
		location->street_address->street_number);
		while (location->street_address->address_line[j] != NULL)
		{
			i += sprintf(_location + i, "Street[%i]: %s\n", j, 
				location->street_address->address_line[j]);
			j++;
		}
		i += sprintf(_location + i, "City: %s\n", location->street_address->city);
		i += sprintf(_location + i, "State: %s\n", location->street_address->state.name);
		i += sprintf(_location + i, "State Code: %s\n", location->street_address->state.code);
		i += sprintf(_location + i, "Postal Code: %s\n", location->street_address->postal_code);
		i += sprintf(_location + i, "County: %s\n", location->street_address->county);
		i += sprintf(_location + i, "Province: %s\n", location->street_address->province);
		i += sprintf(_location + i, "Region: %s\n", location->street_address->region);
		i += sprintf(_location + i, "Country: %s\n", location->street_address->country.name);
		i += sprintf(_location + i, "Country Code: %s\n", location->street_address->country.code);
	}
	#endif
	_wps_free_location(location);
	return result;
}

/**
 * Load the wpsapi library
 */
static int wpsapi_library_load()
{
	_context->logger(LOG_MSG, "Loading %s", _wpsapi_lib_path);
	wpsapi_lib = dlopen(_wpsapi_lib_path, RTLD_NOW /* RTLD_LAZY */);
	if (wpsapi_lib == NULL)
	{
		_context->logger(LOG_ERR, "Cannot load library: %s", dlerror());
		return WPS_PROVIDER_FAILURE;
	}
	_context->logger(LOG_MSG, "Loading symbols");
	_wps_load = (_wps_load_func) dlsym(wpsapi_lib, "WPS_load");
	if (_wps_load == NULL)
	{
		_context->logger(LOG_ERR, "Could not load symbol: %s", dlerror());
		return WPS_PROVIDER_FAILURE;
	}
	_wps_set_key = (_wps_set_key_func) dlsym(wpsapi_lib, "WPS_set_key");
	if (_wps_set_key == NULL)
	{
		_context->logger(LOG_ERR, "Could not load symbol: %s", dlerror());
		return WPS_PROVIDER_FAILURE;
	}
	_wps_location = (_wps_location_func) dlsym(wpsapi_lib, "WPS_location");
	if (_wps_location == NULL)
	{
		_context->logger(LOG_ERR, "Could not load symbol: %s", dlerror());
		return WPS_PROVIDER_FAILURE;
	}
	_wps_free_location = (_wps_free_location_func) dlsym(wpsapi_lib, "WPS_free_location");
	if (_wps_free_location == NULL)
	{
		_context->logger(LOG_ERR, "Could not load symbol: %s", dlerror());
		return -1;
	}

	_context->logger(LOG_MSG, "Initializing wpsapi...");
	_wps_load();
	_wps_set_key(API_KEY);
	return WPS_PROVIDER_SUCCESS;
}

/**
 * Initialize the provider
 */
int provider_init(struct wps_context *context)
{
	if (context == NULL)
		return WPS_PROVIDER_FAILURE;
	_context = context;
	if (_wpsapi_lib_path == NULL)
		_wpsapi_lib_path = WPSAPI_LIB;
	return wpsapi_library_load();
}
