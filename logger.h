#ifndef __LOGGER_H__
#define __LOGGER_H__

#define LOG_MSG 2
#define LOG_WRN 1
#define LOG_ERR 0

void log_message(int level, const char *fmt, ...);

void log_set_level(int level);

#endif
