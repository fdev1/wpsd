AC_INIT([Unified Position Provider], [0.0.1], [http://github.com/fernando-rodriguez/wpsd])
AC_CONFIG_SRCDIR(uppd.c)
AC_PROG_CC
dnl AC_PROG_INSTALL
AC_PROG_SED
AC_PATH_PROG([INSTALL], [install])
AC_HEADER_STDC
AC_CHECK_HEADERS(unistd.h)
AC_ARG_ENABLE([autostart],
	AS_HELP_STRING([--enable-autostart], [Add enable/disable autostart option in settings dialog]))
if test x$enable_autostart = xno ; then
	autostart=0
else
	autostart=1
fi

SRCS="upp.c uppd.c logger.c utils.c wpsapi.c"
AC_SUBST(autostart)
AC_SUBST(SRCS)
AC_CONFIG_FILES(Makefile)
AC_OUTPUT
