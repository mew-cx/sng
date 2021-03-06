dnl Autoconfigure input file for sng
dnl Eric S. Raymond <esr@thyrsus.com>
dnl
dnl Process this file with autoconf to produce a configure script.
dnl

AC_INIT([sng], [1.1.0])
AC_CONFIG_SRCDIR([sng.h])  dnl A distinctive file to look for in srcdir.
AM_INIT_AUTOMAKE([-Wall foreign])

AM_CONFIG_HEADER(config.h)

AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_CPP			dnl Later checks need this.
AC_PROG_CC_C_O
AC_HEADER_STDC

AC_ARG_WITH(png,[  --with-png=DIR             location of png lib/inc],
		[LDFLAGS="${LDFLAGS} -L${withval}"
		CFLAGS="${CFLAGS} -I${withval}"])
  
AC_ARG_WITH(png-lib,[  --with-png-lib=DIR         location of png library],
		[LDFLAGS="${LDFLAGS} -L${withval}"])
                
AC_ARG_WITH(png-inc,[  --with-png-inc=DIR         location of the libpng include files],
                [CFLAGS="${CFLAGS} -I${withval}"])

AC_ARG_WITH(rgbtxt, [  --with-rgbtxt=DIR         location of a color database])

AC_CHECK_LIB(z, deflate)
AC_CHECK_LIB(m, pow)
AC_CHECK_LIB(png, png_get_io_ptr, , , $LIBS)

if test "$ac_cv_lib_png_png_write_init" = "no"
then
    AC_ERROR([PNG library is missing! Please get it.])
fi
if test "$ac_cv_lib_z_deflate" = "no"
then
    AC_ERROR([ZLIB library is missing! Please get it.])
fi

if test -n "$with_rgbtxt"
then
    AC_DEFINE_UNQUOTED(RGBTXT, "$with_rgbtxt", [Location of X color database])
else
    for dir in /etc/X11/ /usr/share/X11 /usr/X11R6/lib/X11 /usr/lib/X11 /usr/openwin/lib /usr/X11/share/X11
    do
	if test -f ${dir}/rgb.txt
	then
	    with_rgbtxt="${dir}/rgb.txt"
	    AC_DEFINE_UNQUOTED(RGBTXT, "$with_rgbtxt", [Location of X color database])
	fi
    done
fi

AC_MSG_CHECKING("for X color database")
if test -f "$with_rgbtxt"
then
   AC_MSG_RESULT([found at $with_rgbtxt])
else
   AC_ERROR([$with_rgbtxt isn't there.])
fi

AC_OUTPUT(Makefile)

dnl Local Variables:
dnl comment-start: "dnl "
dnl comment-end: ""
dnl comment-start-skip: "\\bdnl\\b\\s *"
dnl compile-command: "make configure config.h.in"
dnl End:
