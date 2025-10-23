# SPDX-License-Identifier: GPL-2.0-or-later
#
# Process this file with autoconf to produce a configure script.
#
AC_PREREQ([2.72])

AC_INIT([plainmouth],[0.1],[legion@kernel.org])

AC_CONFIG_AUX_DIR(config)
AC_CONFIG_MACRO_DIR([config/m4])

AC_CONFIG_SRCDIR([src/ipc.c])
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CC

# Checks for libraries.
AX_PTHREAD([], [AC_MSG_ERROR([pthreads could not be found])])

# Checks for header files.
AC_CHECK_HEADERS([stdint.h sys/socket.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_CHECK_HEADER_STDBOOL
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_CHECK_FUNCS([memchr memmove socket strchr strdup strndup])

AC_MSG_NOTICE([checking for pkg-config...])

m4_ifndef([PKG_PROG_PKG_CONFIG], [
	m4_fatal([Could not locate the pkg-config autoconf macros.

These are usually located in /usr/share/aclocal/pkg.m4. If your macros are in a
different location, try setting the environment variable AL_OPTS="-I/other/macro/dir"
before running ./autogen.sh .

Make sure pkg-config is installed.
])])

PKG_PROG_PKG_CONFIG

PKG_CHECK_MODULES([NCURSES], [ncursesw], [], [AC_MSG_ERROR([libncursesw (from ncurses) could not be found])])
PKG_CHECK_MODULES([PANEL],   [panelw],   [], [AC_MSG_ERROR([libpanelw (from ncurses) could not be found])])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
