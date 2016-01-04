#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include <bluetooth/rfcomm.h>

#include "upp_provider.h"

struct wps_context *_context = NULL;

static int connect_to_gps()
{
	inquiry_info *ii = NULL;
	int dev_id, sock, len, max_rsp, num_rsp, flags, i;
	char addr[19] = { '\0' };
	char name[248] = { '\0' };

	dev_id = hci_get_route(NULL);
	sock = hci_open_dev(dev_id);
	if (dev_id < 0 || sock < 0)
	{
		perror("opening socket");
		return -1;
	}
	len = 8;
	max_rsp = 255;
	flags = IREQ_CACHE_FLUSH;
	ii = (inquiry_info*) malloc(max_rsp * sizeof(inquiry_info));
	if (ii == NULL)
	{
		fprintf(stderr, "Out of memory\n");
		return -1;
	}
	num_rsp = hci_inquiry(dev_id, len, max_rsp, NULL, &ii, flags);
	if (num_rsp < 0)
	{
		fprintf(stderr, "hci_inquiry() failed\n");
		free(ii);
		return -1;
	}
	for (i = 0; i < num_rsp; i++)
	{
		ba2str(&(ii + i)->bdaddr, addr);
		memset(name, 0, sizeof(name));
		if (hci_read_remote_name(sock, &(ii+i)->bdaddr, sizeof(name), name, 0) < 0)
			strcpy(name, "[unknown]");
		_context->logger(LOG_MSG, "%s %s\n", addr, name);
	}
	free(ii);
	close(sock);
	return 0;
}

struct wps_location *provider_get_location(int address_lookup)
{
	return NULL;
}

int provider_init(struct wps_context *context)
{
	_context = context;
	if (connect_to_gps() == -1)
		return WPS_PROVIDER_FAILURE;
	return WPS_PROVIDER_SUCCESS;
}
