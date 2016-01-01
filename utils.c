#include <stdlib.h>
#include <unistd.h>

char *fd_readline(int fd, char *buf, int len)
{
	char *bufptr;
	int buflen = len;
	bufptr = buf;
	while (len > 0 && read(fd, bufptr, 1) == 1)
	{
		if (*bufptr == '\n')
		{
			*bufptr = '\0';
			return buf;
		}
		bufptr++;
		len--;
	}
	if (len < 1 || len == buflen)
		return NULL;
	*bufptr = '\0';
	return buf;
}

char *strstrip(char *str, const char *strip_chars)
{
	char *strptr, *strip_chars_ptr, stripped;
	if (str == NULL)
		return NULL;
	strptr = str;
	while (*strptr != '\0')
		strptr++;
	strptr--;
	stripped = 1;
	while (stripped && strptr >= str)
	{
		stripped = 0;
		strip_chars_ptr = (char*) strip_chars;
		while (*strip_chars_ptr != '\0')
		{
			if (*strptr == *strip_chars_ptr)
			{
				stripped = 1;
				*strptr-- = '\0';
				break;
			}
			strip_chars_ptr++;
		}
	}
	stripped = 1;
	strptr = str;
	while (stripped && *strptr != '\0')
	{
		stripped = 0;
		strip_chars_ptr = (char*) strip_chars;
		while (*strip_chars_ptr != '\0')
		{
			if (*strptr == *strip_chars_ptr)
			{
				stripped = 1;
				strptr++;
				break;
			}
			strip_chars_ptr++;
		}
	}
	return strptr;
}

char *split(char *str, char split_char)
{
	if (str == NULL)
		return NULL;
	while (*str != split_char && *str != '\0')
		str++;
	if (*str == split_char)
		*str++ = '\0';
	return str;
}
