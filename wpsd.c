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
#include <dirent.h>
#include "utils.h"
#include "logger.h"
#include "provider.h"

#define SOCKET_NAME "/tmp/wpsd.socket"
#define WPSAPI_LIB "/usr/lib64/wpsapi/libwpsapi.so"
#define API_KEY "eJwz5DQ0AAFTA2NjzmoLcwtnVxNXF10zS0sLXVMLMzddZyNnN11zNwsXR0tjoICjUy0AFI4LWw"
#define PROVIDERS_DIR "/usr/lib/wpsd/providers"

#ifdef SYSCONFDIR
#define CONFIG_FILE SYSCONFDIR "/wpsd.conf"
#else
#define CONFIG_FILE "/etc/wpsd.conf"
#endif

struct wps_provider
{
	int (*init)(struct wps_context *context);
	struct wps_location* (*get_location)(int address_lookup);
	void (*destroy)();
	struct wps_provider *next;
};

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
static char _address_lookup = 1;
static char *_socket_path = NULL;
static char *_wpsapi_lib_path = NULL;
static char *_config_file = NULL;
static unsigned long _next_update = 0;
static unsigned int _update_interval = 10;
static struct wps_provider *providers = NULL;

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
		WPS_StreetAddressLookup address_lookup =
			(_address_lookup) ? WPS_FULL_STREET_ADDRESS_LOOKUP : WPS_NO_STREET_ADDRESS_LOOKUP;
		ret = _wps_location(NULL, address_lookup, &location);
		if (ret != WPS_OK)
		{
			log_message(LOG_ERR, "Call to WPS_location() failed");
			_location[0] = '\0';
			return;
		}
		i += sprintf(_location + i, "Latitude: %lf\n", location->latitude);
		i += sprintf(_location + i, "Longitude: %lf\n", location->longitude);
		i += sprintf(_location + i, "APs: %i\n", location->nap);
		i += sprintf(_location + i, "Accuracy: %lf\n", location->hpe);
		i += sprintf(_location + i, "Speed: %lf\n", location->speed);
		i += sprintf(_location + i, "Bearing: %lf\n", location->bearing);
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
		_wps_free_location(location);
		_next_update = (unsigned long) time(NULL) + _update_interval;
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

	log_message(LOG_MSG, "Preparing to listen on %s", _socket_path);
	fd_socket = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd_socket == -1)
		return -1;

	memset(&addr, 0, sizeof(struct sockaddr_un));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, _socket_path, sizeof(addr.sun_path) - 1);
	if (bind(fd_socket, (struct sockaddr*) &addr, sizeof(addr)) == -1)
	{
		log_message(LOG_ERR, "Call to bind() failed -- Daemon already running?");
		return -1;
	}
	if (listen(fd_socket, 5) == -1)
	{
		log_message(LOG_ERR, "Call to listen() failed");
		return -1;
	}
	
	if (chmod(SOCKET_NAME, 766) == -1)
		log_message(LOG_WRN, "Could not set socket permissions");
	log_message(LOG_MSG, "Listening on %s", _socket_path);

	while (1)
	{
		if ((fd_client = accept(fd_socket, NULL, NULL)) == -1)
			continue;
		update_location();
		if (write(fd_client, _location, strlen(_location)) == -1)
			log_message(LOG_ERR, "Call to write() failed");
		close(fd_client);
	}
}

/**
 * Load the wpsapi library
 */
static int wpsapi_library_load()
{
	log_message(LOG_MSG, "Loading %s", _wpsapi_lib_path);
	wpsapi_lib = dlopen(_wpsapi_lib_path, RTLD_NOW /* RTLD_LAZY */);
	if (wpsapi_lib == NULL)
	{
		log_message(LOG_ERR, "Cannot load library: %s", dlerror());
		return -1;
	}
	log_message(LOG_MSG, "Loading symbols");
	_wps_load = (_wps_load_func) dlsym(wpsapi_lib, "WPS_load");
	if (_wps_load == NULL)
	{
		log_message(LOG_ERR, "Could not load symbol: %s", dlerror());
		return -1;
	}
	_wps_set_key = (_wps_set_key_func) dlsym(wpsapi_lib, "WPS_set_key");
	if (_wps_set_key == NULL)
	{
		log_message(LOG_ERR, "Could not load symbol: %s", dlerror());
		return -1;
	}
	_wps_location = (_wps_location_func) dlsym(wpsapi_lib, "WPS_location");
	if (_wps_location == NULL)
	{
		log_message(LOG_ERR, "Could not load symbol: %s", dlerror());
		return -1;
	}
	_wps_free_location = (_wps_free_location_func) dlsym(wpsapi_lib, "WPS_free_location");
	if (_wps_free_location == NULL)
	{
		log_message(LOG_ERR, "Could not load symbol: %s", dlerror());
		return -1;
	}

	log_message(LOG_MSG, "Initializing wpsapi...");
	_wps_load();
	_wps_set_key(API_KEY);
	return 0;
}

static int load_providers()
{
	DIR *dir;
	struct dirent *entry;
	struct wps_context *context;

	log_message(LOG_MSG, "Loading providers...");
	context = (struct wps_context*) malloc(sizeof(struct wps_context));
	if (context == NULL)
	{
		log_message(LOG_ERR, "Out of memory: malloc() failed.");
		return -1;
	}
	context->logger = &log_message;
	if ((dir = opendir(PROVIDERS_DIR)) == NULL)
	{
		log_message(LOG_ERR, "Could not open providers directory.");
		return -1;
	}
	while ((entry = readdir(dir)))
	{
		log_message(LOG_MSG, "Loading %s...", entry->d_name);
	}
	closedir(dir);
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
		log_message(LOG_ERR, "Call to fork() failed");
		return -1;
	}
	else if (pid != 0)
	{
		return 1;
	}
	if (setsid() == -1)
		log_message(LOG_WRN, "Call to setsid() failed");

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

	if (_config_file == NULL)
		_config_file = CONFIG_FILE;
	log_message(LOG_MSG, "Reading config file: %s", _config_file);

	if ((fd = open(_config_file, O_RDONLY)) == -1)
	{
		log_message(LOG_ERR, "Could not open %s", _config_file);
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
			log_message(LOG_MSG, "Update interval set to %i", _update_interval);
		}
		else if (!strcasecmp(line_ptr, "socket"))
		{
			if (_socket_path != NULL)
				free(_socket_path);
			_socket_path = strdup(value_ptr);
			log_message(LOG_MSG, "Socket path set to %s", _socket_path);
		}
		else if (!strcasecmp(line_ptr, "wpsapi_library"))
		{
			if (_wpsapi_lib_path != NULL)
				free(_wpsapi_lib_path);
			_wpsapi_lib_path = strdup(value_ptr);
			log_message(LOG_MSG, "Wpsapi library path set to %s",
				_wpsapi_lib_path);
		}
		else if (!strcasecmp(line_ptr, "address_lookup"))
		{
			_address_lookup = (!strcasecmp(value_ptr, "yes")) ? 1 : 0;
			log_message(LOG_MSG, "Address lookup %s",
				(_address_lookup) ? "enabled" : "disabled");
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
	printf("  --config\tUse specified config file\n");
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
			log_set_level(LOG_MSG);
		}
		else if (!strcmp("--test", argv[i]))
		{
			load_providers();
			//log_message(LOG_ERR, "--test option not implemented");
			return -1;
		}
		else if (!strcmp("--help", argv[i]))
		{
			print_usage();
			return 0;
		}
		else if (!strcmp("--config", argv[i]))
		{
			if (++i >= argc)
			{
				log_message(LOG_ERR, "Invalid option: --config must be followed by a file path");
				print_usage();
				return -1;
			}
			_config_file = strdup(argv[i]);
			if (_config_file == NULL)
			{
				log_message(LOG_ERR, "Call to strdup() failed");
				return -1;
			}
		}
		else if (!strcmp("--version", argv[i]))
		{
			printf("%s Daemon Version %s\n",
				PACKAGE_NAME, PACKAGE_VERSION);
			printf("Copyright 2015-2016 Fernando Rodriguez\n");
			printf("Please report bugs to %s\n", PACKAGE_BUGREPORT);
			return 0;
		}
		else
		{
			log_message(LOG_ERR, "Invalid option: %s", argv[i]);
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
