#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#define SOCKET_NAME "/tmp/wpsd.socket"

void print_message(const char *fmt, ...)
{
	va_list va;
	va_start(va, fmt);
	fprintf(stderr, "wps: ");
	vfprintf(stderr, fmt, va);
	fprintf(stderr, "\n");
	va_end(va);
}

int main()
{
	int fd, bytes_read;
	struct sockaddr_un addr;
	char buf[512];
	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd == -1)
	{
		print_message("Could not open socket.");
		return -1;
	}
	memset(&addr, 0, sizeof(struct sockaddr_un));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, SOCKET_NAME, sizeof(addr.sun_path) -1);
	
	if (connect(fd, (struct sockaddr*) &addr, sizeof(struct sockaddr_un)) == -1)
	{
		print_message("Could not open socket: %s", SOCKET_NAME);
		return -1;
	}

	while ((bytes_read = read(fd, buf, 512)) > 0)
	{
		if (write(0, buf, bytes_read) == -1)
		{
			print_message("^write() failed");
			break;
		}
	}

	close(fd);
	return 0;
}
