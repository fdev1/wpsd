#ifndef __UTILS_H__
#define __UTILS_H__

char *fd_readline(int fd, char *buf, int len);

char *strstrip(char *str, const char *strip_chars);

char *split(char *str, char split_char);

int daemonize();

#endif
