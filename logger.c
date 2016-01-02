#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include "logger.h"

static int _log_level = 2;

void log_message(int level, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	if (level <= _log_level)
	{
		fprintf(stderr, "wpsd: ");
		vfprintf(stderr, fmt, ap);
		fprintf(stderr, "\n");
	}
}

void log_set_level(int level)
{
	_log_level = level;
}
