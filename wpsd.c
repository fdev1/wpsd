#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <wpsapi.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdarg.h>
#include <dlfcn.h>
#include <time.h>
#include <sys/stat.h>
#include <assert.h>

#define SOCKET_NAME "/tmp/wpsd.socket"
#define WPSAPI_LIB "/usr/lib64/wpsapi/libwpsapi.so"
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

static char _location[2048] = "";
static char _daemonized = 0;
static unsigned long _next_update = 0;
static unsigned int _update_interval = 10;

/**
 * Print debug messages to stdout or syslog
 */
static int print_message(const char *fmt, ...)
{
	va_list ap;
	int ret = 0;
	va_start(ap, fmt);
	if (!_daemonized)
	{
		ret +=  fprintf(stderr, "wpsd[%i]: ", getpid());
		ret += vfprintf(stderr, fmt, ap);
		ret +=  fprintf(stderr, "\n");
	}
	va_end(ap);
	return ret;
}

/**
 * Update the cached location
 */
static void update_location()
{
	if (_next_update <= (unsigned long) time(NULL))
	{
		int i = 0, j = 0;
		WPS_ReturnCode ret;
		WPS_Location *location;
		ret = _wps_location(NULL, WPS_FULL_STREET_ADDRESS_LOOKUP, &location);
		if (ret != WPS_OK)
		{
			_location[0] = '\0';
			return;
		}
		i += sprintf(_location + i, "Latitude: %lf\n", location->latitude);
		i += sprintf(_location + i, "Longitude: %lf\n", location->longitude);
		i += sprintf(_location + i, "APs: %i\n", location->nap);
		i += sprintf(_location + i, "Accuracy: %lf\n", location->hpe);
		i += sprintf(_location + i, "Speed: %lf\n", location->speed);
		i += sprintf(_location + i, "Bearing: %lf\n", location->bearing);
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
		_next_update = (unsigned long) time(NULL) + _update_interval;
		_wps_free_location(location);
	}
}

/**
 * Listen for new connections
 */
static int start_listening()
{
	int fd_socket, fd_client;
	struct sockaddr_un addr;

	unlink(SOCKET_NAME);

	fd_socket = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd_socket == -1)
		return -1;

	memset(&addr, 0, sizeof(struct sockaddr_un));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, SOCKET_NAME, sizeof(addr.sun_path) - 1);
	if (bind(fd_socket, (struct sockaddr*) &addr, sizeof(addr)) == -1)
		return -1;
	if (listen(fd_socket, 5) == -1)
		return -1;
	
	if (chmod(SOCKET_NAME, 766) == -1)
		print_message("Could not set socket permissions");

	while (1)
	{
		if ((fd_client = accept(fd_socket, NULL, NULL)) == -1)
			continue;
		update_location();
		if (write(fd_client, _location, strlen(_location)) == -1)
		{
			print_message("write() failed");
		}
		close(fd_client);
	}
}

/**
 * Load the wpsapi library
 */
static int wpsapi_library_load()
{
	wpsapi_lib = dlopen(WPSAPI_LIB, RTLD_NOW /* RTLD_LAZY */);
	if (wpsapi_lib == NULL)
	{
		print_message("%s", dlerror());
		return -1;
	}
	_wps_load = (_wps_load_func) dlsym(wpsapi_lib, "WPS_load");
	if (_wps_load == NULL)
	{
		print_message("%s", dlerror());
		return -1;
	}
	_wps_set_key = (_wps_set_key_func) dlsym(wpsapi_lib, "WPS_set_key");
	if (_wps_set_key == NULL)
	{
		print_message("%s", dlerror());
		return -1;
	}
	_wps_location = (_wps_location_func) dlsym(wpsapi_lib, "WPS_location");
	if (_wps_location == NULL)
	{
		print_message("%s", dlerror());
		return -1;
	}
	_wps_free_location = (_wps_free_location_func) dlsym(wpsapi_lib, "WPS_free_location");
	if (_wps_free_location == NULL)
	{
		print_message("%s", dlerror());
		return -1;
	}
	_wps_load();
	_wps_set_key(API_KEY);
	return 0;
}

/**
 * Daemonize
 */
static int daemonize()
{
	int pid;
	if ((pid = fork()) == -1)
	{
		print_message("fork() failed");
		return -1;
	}
	else if (pid != 0)
	{
		return 1;
	}
	if (setsid() == -1)
		print_message("setsid() failed");

	close(2);
	close(1);
	close(0);

	_daemonized = 1;
	return 0;
}

int main(int argc, char **argv)
{
	/* load libwpsapi.so */
	if (wpsapi_library_load() == -1)
		return -1;

	if (argc > 1 && !strcmp(argv[1], "--daemon"))
	{
		int pid;
		if ((pid = daemonize()) == -1)
			return -1;
		else if (pid == 1)
			return 0;
	}
	return start_listening();
}
