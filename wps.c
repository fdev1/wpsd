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

int print_raw_socket()
{
	int fd, bytes_read;
	struct sockaddr_un addr;
	char buf[512];
	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd == -1)
	{
		print_message("socket() failed");
		return -1;
	}
	memset(&addr, 0, sizeof(struct sockaddr_un));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, SOCKET_NAME, sizeof(addr.sun_path) -1);
	
	if (connect(fd, (struct sockaddr*) &addr, sizeof(struct sockaddr_un)) == -1)
	{
		print_message("Daemon not running");
		print_message("Socket: %s", SOCKET_NAME);
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

void print_usage()
{
	printf("usage: wps\n");
	printf("\n");
	printf("Options:\n");
	printf("  --socket\tUse the specified socket.\n");
	printf("  --version\tPrint version information.\n");
	printf("  --help\tPrint this help.\n");
}

int main(int argc, char **argv)
{
	int i;
	for (i = 1; i < argc; i++)
	{
		if (!strcmp("--help", argv[i]) || !strcmp("-h", argv[i]))
		{
			print_usage();
			return 0;
		}
		else if (!strcmp("--version", argv[i]) || !strcmp("-V", argv[i]))
		{
			printf("%s Utility Version %s\n",
				PACKAGE_NAME, PACKAGE_VERSION);
			printf("Copyright 2015-2016 Fernando Rodriguez\n");
			printf("Please report bugs to %s\n", PACKAGE_BUGREPORT);
			return 0;
		}
		else
		{
			print_message("Invalid option: %s", argv[i]);
			print_usage();
			return -1;
		}
	}
	return print_raw_socket();
}
