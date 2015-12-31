AC_INIT([wpsd.c])
AC_PROG_CC
AC_PROG_INSTALL
#AC_PATH_PROG([INSTALL], [install])
AC_HEADER_STDC
AC_CHECK_HEADERS(unistd.h string.h pwd.h sys/stat.h sys/types.h)
AC_ARG_ENABLE([autostart],
	AS_HELP_STRING([--enable-autostart], [Add enable/disable autostart option in settings dialog]))
if test x$enable_autostart = xno ; then
	autostart=0
else
	autostart=1
fi
AC_SUBST(autostart)
AC_OUTPUT(Makefile)
