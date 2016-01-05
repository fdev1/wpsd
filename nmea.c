#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <time.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include <bluetooth/sdp.h>
#include <bluetooth/sdp_lib.h>
#include <bluetooth/rfcomm.h>
#include "upp_provider.h"
#include "utils.h"

#define ENABLE_BLUETOOTH
#define VARDIR "/var/lib/upp"

static struct wps_context *_context = NULL;
static struct wps_location _location;
static int _gps_socket = -1;
static int _scan_interval = 0;
static char _connected = 0;
static pthread_t _worker = NULL;

/**
 * Parse NMEA sentence
 */
static char *nmea_next_field(char *sentence, char**next)
{
	char *psentence = sentence;
	if (sentence == NULL)
	{
		*next = NULL;
		return NULL;
	}
	while (*psentence != ',' && *psentence != '\0')
		psentence++;
	if (*psentence == ',')
	{
		*next = psentence + 1;
		*psentence = '\0';
	}
	return sentence;
}

/**
 * Convert nmea time field to time_t
 */
static time_t nmea_time_to_time_t(char *utc)
{
	struct tm *tm;
	time_t now;
	//time_t diff;
	now = time(NULL);
	//diff = mktime(localtime(&now)) - mktime(gmtime(&now));
	tm = gmtime(&now);
	tm->tm_hour = tm->tm_min = tm->tm_sec = 0;
	tm->tm_hour += (*utc++ - 0x30) * 10;
	tm->tm_hour += (*utc++ - 0x30) * 1;
	tm->tm_min  += (*utc++ - 0x30) * 10;
	tm->tm_min  += (*utc++ - 0x30) * 1;
	tm->tm_sec  += (*utc++ - 0x30) * 10;
	tm->tm_sec  += (*utc++ - 0x30) * 1;
	//return mktime(tm) + diff;
	return mktime(tm);
}

/**
 * Converts coordinates from degrees decimal minutes
 * to decimal minutes.
 */
static double nmea_ddm_to_dec(char *coords, char direction)
{
	int i;
	char degrees[6];
	double degrees_decimal;
	for (i = 0; coords[i] != '.' && i < 5; i++)
		degrees[i] = coords[i];
	degrees[i] = degrees[i - 1] = degrees[i - 2] = '\0';
	coords += i - 2;
	sscanf(coords, "%lf", &degrees_decimal);
	degrees_decimal /= (double) 60.0;
	degrees_decimal += (double) atoi(degrees);
	if (direction == 'W' || direction == 'S')
		degrees_decimal -= (degrees_decimal * 2);
	return degrees_decimal;
}

#ifdef ENABLE_BLUETOOTH

/**
 * Save the paired device address so we can
 * connect to it later without it being discoverable
 * TODO: Is there a better way?
 */
static void save_bt_device(char *addr)
{
	#define BUFSZ (256)
	int fd;
	char filename[BUFSZ];
	char read_addr[19];
	const char newline = '\n';
	_context->logger(LOG_MSG, "Saving %s", addr);
	strncpy(filename, VARDIR, BUFSZ);
	strncat(filename, "/known_devices", BUFSZ);
	if ((fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR | S_IROTH)) == -1)
	{
		_context->logger(LOG_ERR, "Could not open %s", filename);
		return;
	}

	while (fd_readline(fd, &read_addr[0], 19) != NULL)
	{
		if (!strcmp(read_addr, addr))
		{
			close(fd);
			return;
		}
	}
	if (write(fd, addr, strlen(addr)) == -1)
	{
		_context->logger(LOG_ERR, "Could not write to %s", filename);
		perror("write()");
		return;
	}
	if (write(fd, &newline, 1) == -1)
	{
		_context->logger(LOG_ERR, "Could not write to %s", filename);
		return;
	}
	close(fd);
	return;
	#undef BUFSZ
}

/**
 * Detect and connect to bt gps receivers
 */
static int connect_to_bt_gps()
{
	inquiry_info *ii = NULL;
	int dev_id, sock, len, max_rsp, num_rsp, flags, i;
	char addr[19] = { '\0' };
	char name[248] = { '\0' };
	/* 00001101-0000-1000-8000-00805f9b34fb */
	uint8_t svc_uuid_int[] =
	{
		0x00, 0x00, 0x11, 0x01, 0x00, 0x00, 0x10, 0x00,
		0x80, 0x00, 0x00, 0x80, 0x5f, 0x9b, 0x34, 0xfb 
	};
	uuid_t svc_uuid;
	sdp_list_t *r, *p, *pds, *proto_list;
	sdp_data_t *d;
	uint32_t range = 0x0000ffff;
	int err;

	_context->logger(LOG_WRN, "Scanning for bluetooth GPS...");
	
	/* initialize uuid */
	sdp_uuid128_create(&svc_uuid, &svc_uuid_int);

	pthread_mutex_lock(_context->wireless_lock);

	dev_id = hci_get_route(NULL);
	sock = hci_open_dev(dev_id);
	if (dev_id < 0 || sock < 0)
	{
		_context->logger(LOG_ERR, "Error opening socket");
		perror("opening socket");
		pthread_mutex_unlock(_context->wireless_lock);
		return -1;
	}
	len = 8;
	max_rsp = 255;
	flags = IREQ_CACHE_FLUSH;
	ii = (inquiry_info*) malloc(max_rsp * sizeof(inquiry_info));
	if (ii == NULL)
	{
		_context->logger(LOG_ERR, "Out of memory: malloc() failed");
		pthread_mutex_unlock(_context->wireless_lock);
		return -1;
	}
	num_rsp = hci_inquiry(dev_id, len, max_rsp, NULL, &ii, flags);
	if (num_rsp < 0)
	{
		_context->logger(LOG_WRN, "hci_inquiry() failed");
		pthread_mutex_unlock(_context->wireless_lock);
		free(ii);
		return -1;
	}
	for (i = 0; !_connected && i < num_rsp; i++)
	{
		ba2str(&(ii + i)->bdaddr, addr);
		memset(name, 0, sizeof(name));
		if (hci_read_remote_name(sock, &(ii+i)->bdaddr, sizeof(name), name, 0) < 0)
			strcpy(name, "[unknown]");
		_context->logger(LOG_MSG, ">> %s %s", addr, name);

		sdp_list_t *response_list = NULL, *search_list, *attrid_list;
		sdp_session_t *session = 0;

		// connect to the SDP server running on the remote machine
		// specify the UUID of the application we're searching for
		// specify that we want a list of all the matching applications' attributes
		// get a list of service records that have UUID 0xabcd
		session = sdp_connect(BDADDR_ANY, &(ii + i)->bdaddr, SDP_RETRY_IF_BUSY);
		search_list = sdp_list_append(NULL, &svc_uuid);
		attrid_list = sdp_list_append(NULL, &range);
		err = sdp_service_search_attr_req(session, search_list,
			SDP_ATTR_REQ_RANGE, attrid_list, &response_list);
		if (err)
			_context->logger(LOG_WRN, "Error %i", err);

		// go through each of the service records
		for (r = response_list; !_connected && r != NULL; r = r->next)
		{
			sdp_record_t *rec = (sdp_record_t*) r->data;

			// get a list of the protocol sequences
			if (sdp_get_access_protos(rec, &proto_list) == 0)
			{
				// go through each protocol sequence
				for (p = proto_list; !_connected && p != NULL; p = p->next)
				{
					// go through each protocol list of the protocol sequence
					for(pds = (sdp_list_t*) p->data; !_connected && pds != NULL; pds = pds->next)
					{
						// check the protocol attributes
						int proto = 0;
						for (d = (sdp_data_t*) pds->data; !_connected && d != NULL; d = d->next)
						{
							switch (d->dtd)
							{ 
							case SDP_UUID16:
							case SDP_UUID32:
							case SDP_UUID128:
								proto = sdp_uuid_to_proto( &d->val.uuid );
								break;
							case SDP_UINT8:
								if (proto == RFCOMM_UUID)
								{
									_context->logger(LOG_MSG, "rfcomm channel: %d",d->val.int8);
									_gps_socket = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
									if (_gps_socket == -1)
									{
										_context->logger(LOG_ERR, "socket() failed");
									}
									else
									{
										struct sockaddr_rc saddr = { 0 };
										saddr.rc_family = AF_BLUETOOTH;
										saddr.rc_channel = d->val.int8;
										saddr.rc_bdaddr = (ii + i)->bdaddr;
										if (connect(_gps_socket, (struct sockaddr*) &saddr, sizeof(saddr)) == -1)
										{
											_context->logger(LOG_ERR, "connect() failed");
										}
										else
										{
											_connected = 1;
											_context->status = UPP_STATUS_ONLINE;
											_context->logger(LOG_MSG, "Connection established");
											save_bt_device(addr);
										}
									}
								}
								break;
							}
						}
					}
					sdp_list_free((sdp_list_t*)p->data, 0);
				}
				sdp_list_free( proto_list, 0 );
			}
			printf("found service record 0x%x\n", rec->handle);
			sdp_record_free(rec);
		}
		sdp_close(session);
	}
	free(ii);
	close(sock);
	pthread_mutex_unlock(_context->wireless_lock);
	return 0;
}
#endif

static void *nmea_listener(void* arg)
{
	static int scan_interval = 0;
	while (1)
	{
		if (!_connected)
		{
			sleep(scan_interval);
			if (_context->get_idle_time() > 120)
				continue;
			if (connect_to_bt_gps() == -1)
				scan_interval = _scan_interval;
			else
				scan_interval = 30;
		}
		else
		{
			char buf[100], last_char = 0;
			char *field, *sentence = buf;
			int i = 0;
			while (read(_gps_socket, &sentence[i], 1) == 1)
			{
				if (last_char == '\r' && sentence[i] == '\n')
				{
					sentence[i - 1] = '\0';
					break;
				}
				last_char = sentence[i];
				i++;
			}
			if (i == 0 || _context->get_idle_time() > 120)
			{
				_context->logger(LOG_ERR, "Disconnected");
				_context->status = UPP_STATUS_OFFLINE;
				_connected = 0;
				close(_gps_socket);
				_gps_socket = -1;
				continue;
			}

			field = nmea_next_field(sentence, &sentence);
			if (!strcmp("$GPGGA", field))
			{
				double horiz_dilution;
				char unit;

				_location.type = UPP_PROVIDER_TYPE_GPS;
				_location.timestamp = nmea_time_to_time_t(nmea_next_field(sentence, &sentence));

				char *ddm_lat = nmea_next_field(sentence, &sentence);
				char *ddm_lat_dir = nmea_next_field(sentence, &sentence);
				char *ddm_lon = nmea_next_field(sentence, &sentence);
				char *ddm_lon_dir = nmea_next_field(sentence, &sentence);

				_location.latitude = nmea_ddm_to_dec(ddm_lat, ddm_lat_dir[0]);
				_location.longitude = nmea_ddm_to_dec(ddm_lon, ddm_lon_dir[0]);
				_location.accuracy = atoi(nmea_next_field(sentence, &sentence));
				_location.sources = atoi(nmea_next_field(sentence, &sentence));
				sscanf(nmea_next_field(sentence, &sentence), "%lf", &horiz_dilution);
				sscanf(nmea_next_field(sentence, &sentence), "%lf", &_location.altitude);
				unit = *nmea_next_field(sentence, &sentence);
				if (unit != 'M')
					_location.altitude *= 10;

				#if 0
				time_t now = time(NULL);
				printf("GPS Fix Data\n");
				printf("Fix date: %s", asctime(localtime(&_location.timestamp)));
				printf("Now: %s", asctime(localtime(&now)));
				printf("Sentence: %s\n", sentence);
				printf("Latitude: %lf\n", _location.latitude);
				printf("Longitude: %lf\n", _location.longitude);
				printf("Altitude: %lf ", _location.altitude);
				printf("Quality: %lf\n", _location.accuracy);
				printf("Sources: %i\n", _location.sources);
				printf("Horiz Dilution: %lf\n", horiz_dilution);
				printf("Geoid Height: %s ", nmea_next_field(sentence, &sentence));
				printf("%s\n", nmea_next_field(sentence, &sentence));
				printf("Last DGPS Update: %s\n", nmea_next_field(sentence, &sentence));
				printf("DGPS station: %s\n", nmea_next_field(sentence, &sentence));
				printf("=======================================\n");
				#endif
			}
			else if (!strcmp("$GPGLL", field) || !strcmp("$LCGLL", field))
			{
				char *ddm_lat = nmea_next_field(sentence, &sentence);
				char *ddm_lat_dir = nmea_next_field(sentence, &sentence);
				char *ddm_lon = nmea_next_field(sentence, &sentence);
				char *ddm_lon_dir = nmea_next_field(sentence, &sentence);

				_location.type = UPP_PROVIDER_TYPE_GPS;
				_location.latitude = nmea_ddm_to_dec(ddm_lat, ddm_lat_dir[0]);
				_location.longitude = nmea_ddm_to_dec(ddm_lon, ddm_lon_dir[0]);
				_location.timestamp = nmea_time_to_time_t(nmea_next_field(sentence, &sentence));

				#if 0
				printf("Latitude: %lf\n", _location.latitude);
				printf("Longitude: %lf\n", _location.longitude);
				printf("Time: %s", asctime(localtime(&_location.timestamp)));
				#endif
			}
		}
	}
	return NULL;
}

/**
 * Get the latest fix
 */
struct wps_location *provider_get_location(int address_lookup)
{
	if (!_connected)
		return NULL;
	return &_location;
}

/**
 * Initialize provider
 */
int provider_init(struct wps_context *context)
{
	const char *scan_interval;
	_context = context;
	_context->status = UPP_STATUS_OFFLINE;
	memset(&_location, 0, sizeof(struct wps_location));
	scan_interval = _context->get_config(_context, "bluetooth_scan_interval");
	if (scan_interval != NULL)
		_scan_interval = atoi(scan_interval);

	/* start worker thread */
	if (pthread_create(&_worker, NULL, &nmea_listener, (void*) NULL))
	{
		_context->logger(LOG_ERR, "Initialization error: pthread_create() failed");
		return WPS_PROVIDER_FAILURE;
	}
	return WPS_PROVIDER_SUCCESS;
}
