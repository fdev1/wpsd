#include <stdlib.h>
#include <unistd.h>
#include <dlfcn.h>
#include <pthread.h>
#include <string.h>
#include "wpsapi/include/wpsapi.h"
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

static pthread_t _worker = NULL;
static struct wps_context *_context = NULL;
static struct wps_location _location;

/**
 * Keep location up-to-date
 */
static void *worker(void *arg)
{
	WPS_ReturnCode ret;
	WPS_Location *location;
	//WPS_StreetAddressLookup lookup =
	//	(address_lookup) ? WPS_FULL_STREET_ADDRESS_LOOKUP : WPS_NO_STREET_ADDRESS_LOOKUP;
	while (1)
	{
		if (_context->get_idle_time() > 120)
		{
			sleep(10);
			continue;
		}
		ret = _wps_location(NULL, WPS_NO_STREET_ADDRESS_LOOKUP, &location);
		if (ret != WPS_OK)
		{
			_context->logger(LOG_ERR, "Call to WPS_location() failed");
			sleep(20);
		}
		else
		{
			_context->status = UPP_STATUS_ONLINE;
			_location.latitude = location->latitude;
			_location.longitude = location->longitude;
			_location.sources = location->nap;
			_location.accuracy = location->hpe;
			_location.speed = location->speed;
			_location.bearing = location->bearing;
			_location.type = UPP_PROVIDER_TYPE_WIFI;
			_location.timestamp = time(NULL);
			_wps_free_location(location);
			sleep(5);
		}
	}
	return NULL;
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
 * Get the location
 */
struct wps_location *provider_get_location(int address_lookup)
{
	return &_location;
}

/**
 * Initialize the provider
 */
int provider_init(struct wps_context *context)
{
	if (context == NULL)
		return WPS_PROVIDER_FAILURE;
	_context = context;
	_context->status = UPP_STATUS_OFFLINE;
	memset(&_location, 0, sizeof(struct wps_location));
	if (_wpsapi_lib_path == NULL)
		_wpsapi_lib_path = WPSAPI_LIB;
	if (wpsapi_library_load() != WPS_PROVIDER_SUCCESS)
		return WPS_PROVIDER_FAILURE;

	/* start worker thread */
	if (pthread_create(&_worker, NULL, &worker, (void*) NULL))
	{
		_context->logger(LOG_ERR, "Initialization error: pthread_create() failed");
		return WPS_PROVIDER_FAILURE;
	}
	return WPS_PROVIDER_SUCCESS;
}
