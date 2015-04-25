AC_PREREQ(2.61)
AC_INIT([Clamz], [0.5], [floppusmaximus@users.sf.net])
AC_CONFIG_SRCDIR([clamz.h])
AC_CONFIG_HEADER([config.h])

# Checks for programs.
AC_PROG_CC
if test "x$GCC" = "xyes" ; then
   CFLAGS="$CFLAGS -W -Wall"
fi
AC_PROG_INSTALL

AC_ARG_VAR(UPDATE_MIME_DATABASE, [Program to update MIME info database])
AC_CHECK_PROG([UPDATE_MIME_DATABASE], [update-mime-database],
              [update-mime-database], [:])

AC_ARG_VAR(UPDATE_DESKTOP_DATABASE, [Program to update desktop application database])
AC_CHECK_PROG([UPDATE_DESKTOP_DATABASE], [update-desktop-database],
              [update-desktop-database], [:])

# Checks for libraries.
AC_CHECK_LIB([expat], [XML_ParserCreate], [], [AC_MSG_ERROR([libexpat not found])])
PKG_CHECK_MODULES(LIBCURL, libcurl)

AC_ARG_VAR(LIBGCRYPT_CFLAGS, [C compiler flags for libgcrypt])
AC_ARG_VAR(LIBGCRYPT_LIBS, [Linker flags for libgcrypt])
if test "x$LIBGCRYPT_CFLAGS$LIBGCRYPT_LIBS" = "x" ; then
   AM_PATH_LIBGCRYPT([],[],[AC_MSG_ERROR([libgcrypt not found])])
fi

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_SIZE_T
AC_TYPE_OFF_T
AC_SYS_LARGEFILE

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
