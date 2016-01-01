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
#include <sys/types.h>
#include <fcntl.h>
#include <assert.h>
#include "utils.h"

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
static char _verbose = 0;
static char *_socket_path = NULL;
static char *_wpsapi_lib_path = NULL;
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
	if (1 || !_daemonized)
	{
		ret +=  fprintf(stderr, "wpsd: ");
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
	strncpy(addr.sun_path, _socket_path, sizeof(addr.sun_path) - 1);
	if (bind(fd_socket, (struct sockaddr*) &addr, sizeof(addr)) == -1)
	{
		print_message("bind() failed -- Daemon already running?");
		return -1;
	}
	if (listen(fd_socket, 5) == -1)
	{
		print_message("listen() failed");
		return -1;
	}
	
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
	wpsapi_lib = dlopen(_wpsapi_lib_path, RTLD_NOW /* RTLD_LAZY */);
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

/**
 * Read config
 */
static int read_config()
{
	int fd;
	char buf[1024];
	char *line_ptr, *value_ptr;
	if ((fd = open("/etc/wpsd.conf", O_RDONLY)) == -1)
	{
		print_message("could not open wpsd.conf");
		return -1;
	}
	while (fd_readline(fd, buf, 1024) != NULL)
	{
		line_ptr = strstrip(buf, " \t");
		if (*line_ptr == '#' || *line_ptr == ';' || *line_ptr == '\0')
			continue;
		value_ptr = split(line_ptr, '=');
		if (!strcasecmp(line_ptr, "update_interval"))
		{
			_update_interval = atoi(value_ptr);
			if (_verbose)
				print_message("Update interval set to: %i", _update_interval);
		}
		else if (!strcasecmp(line_ptr, "socket"))
		{
			if (_socket_path != NULL)
				free(_socket_path);
			_socket_path = strdup(value_ptr);
			if (_verbose)
				print_message("Socket path set to %s", _socket_path);
		}
		else if (!strcasecmp(line_ptr, "wpsapi_library"))
		{
			if (_wpsapi_lib_path != NULL)
				free(_wpsapi_lib_path);
			_wpsapi_lib_path = strdup(value_ptr);
			if (_verbose)
				print_message("Wpsapi library path set to %s", _wpsapi_lib_path);
		}
	}
	return 0;
}

/**
 * Print command options
 */
static void print_usage()
{
	printf("usage: wpsd [options]\n");
	printf("\n");
	printf("Options:\n");
	printf("  --daemon\tRun as a daemon\n");
	printf("  --test\tTest provider\n");
	printf("  --verbose\tEnable verbose output\n");
}


int main(int argc, char **argv)
{
	int i;
	char do_daemonize = 0;
	for (i = 1; i < argc; i++)
	{
		if (!strcmp("--daemon", argv[i]))
		{
			do_daemonize = 1;
		}
		else if (!strcmp("--verbose", argv[i]))
		{
			_verbose = 1;
		}
		else if (!strcmp("--test", argv[i]))
		{
			print_message("--test option not yet implemented");
			return -1;
		}
		else if (!strcmp("--help", argv[i]))
		{
			print_usage();
			return 0;
		}
		else
		{
			print_message("Invalid option: %s", argv[i]);
			print_usage();
			return -1;
		}
	}
	read_config();
	if (_socket_path == NULL)
		_socket_path = SOCKET_NAME;
	if (_wpsapi_lib_path == NULL)
		_wpsapi_lib_path = WPSAPI_LIB;
	/* load libwpsapi.so */
	if (wpsapi_library_load() == -1)
		return -1;

	if (do_daemonize)
	{
		int pid;
		if ((pid = daemonize()) == -1)
			return -1;
		else if (pid == 1)
			return 0;
	}
	return start_listening();
}
