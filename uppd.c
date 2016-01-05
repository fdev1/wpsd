#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <dlfcn.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <assert.h>
#include <dirent.h>
#include <pthread.h>

#include "utils.h"
#include "logger.h"
#include "upp.h"
#include "upp_provider.h"

#define SOCKET_NAME "/tmp/uppd.socket"
#define PROVIDERS_DIR "/usr/lib/uppd/providers"
#define CONFIG_FILE SYSCONFDIR "/uppd.conf"

struct wps_provider
{
	int (*init)(struct wps_context *context);
	struct wps_location* (*get_location)(int address_lookup);
	struct wps_provider *next;
};

static char _location[2048] = "";
static char _address_lookup = 1;
static char *_socket_path = NULL;
static char *_wpsapi_lib_path = NULL;
static char *_config_file = NULL;
static time_t _last_request = 0;
static unsigned long _next_update = 0;
static unsigned int _update_interval = 10;
static pthread_mutex_t _wireless_lock = PTHREAD_MUTEX_INITIALIZER;

/*
 * Upp context and providers list
 */
static struct wps_context __context;
static struct wps_context *_context = &__context;
static struct wps_provider *_providers = NULL;

/**
 * Update the cached location
 */
static void update_location()
{
	_last_request = time(NULL);
	if (_next_update <= (unsigned long) time(NULL))
	{
		int i = 0;
		struct wps_location *loc = NULL;
		struct wps_provider *provider = _providers;
		do
		{
			loc = provider->get_location(0);
			provider = provider->next;
		}
		while (loc == NULL && provider != NULL);

		if (loc == NULL)
		{
			_context->logger(LOG_ERR, "Could not get location: get_location() failed");
			return;
		}
		i += sprintf(_location + i, "Latitude: %lf\n", loc->latitude);
		i += sprintf(_location + i, "Longitude: %lf\n", loc->longitude);
		i += sprintf(_location + i, "Sources: %i\n", loc->sources);
		i += sprintf(_location + i, "Accuracy: %lf\n", loc->accuracy);
		i += sprintf(_location + i, "Speed: %lf\n", loc->speed);
		i += sprintf(_location + i, "Bearing: %lf\n", loc->bearing);
		i += sprintf(_location + i, "Time: %s", asctime(localtime(&loc->timestamp)));

		switch (loc->type)
		{
		case UPP_PROVIDER_TYPE_WIFI:
			i += sprintf(_location + i, "Type: WIFI\n");
			break;
		case UPP_PROVIDER_TYPE_GPS:
			i += sprintf(_location + i, "Type: GPS\n");
			break;
		default:
			i += sprintf(_location + i, "Type: Unknown\n");
			break;
		}
			
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
		_wps_free_location(location);
		#endif
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

	_context->logger(LOG_MSG, "Preparing to listen on %s", _socket_path);
	fd_socket = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd_socket == -1)
		return -1;

	memset(&addr, 0, sizeof(struct sockaddr_un));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, _socket_path, sizeof(addr.sun_path) - 1);
	if (bind(fd_socket, (struct sockaddr*) &addr, sizeof(addr)) == -1)
	{
		_context->logger(LOG_ERR, "Call to bind() failed -- Daemon already running?");
		return -1;
	}
	if (listen(fd_socket, 5) == -1)
	{
		_context->logger(LOG_ERR, "Call to listen() failed");
		return -1;
	}
	
	if (chmod(SOCKET_NAME, 766) == -1)
		_context->logger(LOG_WRN, "Could not set socket permissions");
	_context->logger(LOG_MSG, "Listening on %s", _socket_path);

	while (1)
	{
		if ((fd_client = accept(fd_socket, NULL, NULL)) == -1)
			continue;
		update_location();
		if (write(fd_client, _location, strlen(_location)) == -1)
			_context->logger(LOG_ERR, "Call to write() failed");
		close(fd_client);
	}
}

static unsigned int get_idle_time()
{
	return (unsigned int) (time(NULL) - _last_request);
}

/**
 * Load location providers
 */
static int load_providers()
{
	DIR *dir;
	struct dirent *entry;
	struct stat filestat;
	char filename[1024];

	_context->logger(LOG_MSG, "Loading providers...");
	if ((dir = opendir(PROVIDERS_DIR)) == NULL)
	{
		_context->logger(LOG_ERR, "Could not open providers directory.");
		return -1;
	}
	while ((entry = readdir(dir)))
	{
		void *handle;
		struct wps_provider *provider;
		if (entry->d_name[0] == '.')
			continue;
		strcpy(filename, PROVIDERS_DIR);
		strcat(filename, "/");
		strcat(filename, entry->d_name);
		if (stat(filename, &filestat) == -1)
			continue;
		if (S_ISDIR(filestat.st_mode) || !S_ISREG(filestat.st_mode))
			continue;
		_context->logger(LOG_MSG, "Loading %s...", filename);
		handle = dlopen(filename, RTLD_NOW);
		if (handle == NULL)
		{
			_context->logger(LOG_ERR, "Could not load provider module %s", entry->d_name);
			_context->logger(LOG_ERR, "%s", dlerror());
			continue;
		}
		provider = malloc(sizeof(struct wps_provider));
		if (provider == NULL)
		{
			_context->logger(LOG_ERR, "Out of memory: malloc() failed");
			continue;
		}
		memset(provider, 0, sizeof(struct wps_provider));
		provider->init = dlsym(handle, "provider_init");
		if (provider->init == NULL)
		{
			_context->logger(LOG_ERR, "Could not load provider_init: %s", dlerror());
			free(provider);
			continue;
		}
		provider->get_location = dlsym(handle, "provider_get_location");
		if (provider->get_location == NULL)
		{
			_context->logger(LOG_ERR, "Could not load provider_get_location: %s", dlerror());
			free(provider);
			continue;
		}
		if (provider->init(_context) != WPS_PROVIDER_SUCCESS)
		{
			_context->logger(LOG_ERR, "Provider init failed");
			continue;
		}
		if (_providers == NULL)
		{
			_providers = provider;
		}
		else
		{
			struct wps_provider *p = _providers;
			while (p->next != NULL)
				p = p->next;
			p->next = provider;
		}
		_context->logger(LOG_MSG, "Provider loaded");
	}
	closedir(dir);
	return 0;
}

/**
 * Get a config entry
 */
static const char *get_config(struct wps_context *context, const char *name)
{
	struct config_entry *entry;
	entry = context->config;
	while (entry != NULL)
	{
		if (!strcasecmp(name, entry->name))
			return entry->value;
		entry = entry->next;
	}
	return NULL;
}

static int add_config(struct wps_context *context,
	const char *name, const char *value)
{
	struct config_entry **entry = &context->config;
	struct config_entry *new_entry = NULL;
	while (*entry != NULL)
	{
		if (!strcasecmp(name, (*entry)->name))
			return -1;
		entry = &(*entry)->next;
	}
	new_entry = (struct config_entry*) malloc(sizeof(struct config_entry));
	if (new_entry == NULL)
	{
		_context->logger(LOG_ERR, "Out of memory: malloc() failed");
		return -1;
	}
	new_entry->name = strdup(name);
	if (new_entry->name == NULL)
	{
		_context->logger(LOG_ERR, "Out of memory: strdup() failed");
		free(new_entry);
		return -1;
	}
	new_entry->value = strdup(value);
	if (new_entry->value == NULL)
	{
		_context->logger(LOG_ERR, "Out of memory: strdup() failed");
		free((void*) new_entry->name);
		free((void*) new_entry);
		return -1;
	}
	*entry = new_entry;
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
	_context->logger(LOG_MSG, "Reading config file: %s", _config_file);

	if ((fd = open(_config_file, O_RDONLY)) == -1)
	{
		_context->logger(LOG_ERR, "Could not open %s", _config_file);
		return -1;
	}
	while (fd_readline(fd, buf, 1024) != NULL)
	{
		line_ptr = strstrip(buf, " \t");
		if (*line_ptr == '#' || *line_ptr == ';' || *line_ptr == '\0')
			continue;
		value_ptr = split(line_ptr, '=');
		line_ptr = strstrip(line_ptr, " \t");
		value_ptr = strstrip(line_ptr, " \t");
		if (!strcasecmp(line_ptr, "update_interval"))
		{
			_update_interval = atoi(value_ptr);
			_context->logger(LOG_MSG, "Update interval set to %i", _update_interval);
		}
		else if (!strcasecmp(line_ptr, "socket"))
		{
			if (_socket_path != NULL)
				free(_socket_path);
			_socket_path = strdup(value_ptr);
			_context->logger(LOG_MSG, "Socket path set to %s", _socket_path);
		}
		else if (!strcasecmp(line_ptr, "wpsapi_library"))
		{
			if (_wpsapi_lib_path != NULL)
				free(_wpsapi_lib_path);
			_wpsapi_lib_path = strdup(value_ptr);
			_context->logger(LOG_MSG, "Wpsapi library path set to %s",
				_wpsapi_lib_path);
		}
		else if (!strcasecmp(line_ptr, "address_lookup"))
		{
			_address_lookup = (!strcasecmp(value_ptr, "yes")) ? 1 : 0;
			_context->logger(LOG_MSG, "Address lookup %s",
				(_address_lookup) ? "enabled" : "disabled");
		}
		if (add_config(_context, line_ptr, value_ptr) == -1)
		{
			_context->logger(LOG_ERR, "Could not add config %s", line_ptr);
		}
	}
	return 0;
}

/**
 * Print command options
 */
static void print_usage()
{
	printf("usage: uppd [options]\n");
	printf("\n");
	printf("Options:\n");
	printf("  --daemon\tRun as a daemon\n");
	printf("  --test\tTest provider\n");
	printf("  --verbose\tEnable verbose output\n");
	printf("  --config\tUse specified config file\n");
}

/**
 * Entry point
 */
int main(int argc, char **argv)
{
	int i;
	char do_daemonize = 0;

	_context->logger = &log_message;
	_context->get_config = &get_config;
	_context->get_idle_time = &get_idle_time;
	_context->wireless_lock = &_wireless_lock;
	_context->config = NULL;
	_last_request = time(NULL);

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
			_context->logger(LOG_ERR, "--test option not implemented");
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
				_context->logger(LOG_ERR, "Invalid option: --config must be followed by a file path");
				print_usage();
				return -1;
			}
			_config_file = strdup(argv[i]);
			if (_config_file == NULL)
			{
				_context->logger(LOG_ERR, "Call to strdup() failed");
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
			_context->logger(LOG_ERR, "Invalid option: %s", argv[i]);
			print_usage();
			return -1;
		}
	}
	read_config();
	if (_socket_path == NULL)
		_socket_path = SOCKET_NAME;
	
	if (load_providers() == -1)
	{
		_context->logger(LOG_ERR, "Could not load providers");
		return -1;
	}

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
