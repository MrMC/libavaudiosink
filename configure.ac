AC_PREREQ(2.50)
AC_INIT([libavaudiosink], [1.0.0], [rootcoder, llc])
AC_CONFIG_HEADERS([config.h])
AM_INIT_AUTOMAKE([-Wall foreign subdir-objects])
AC_CANONICAL_HOST
AC_CONFIG_MACRO_DIR([m4])
m4_pattern_allow([AM_PROG_AR])
AM_PROG_AR

# Work around stupid autoconf default cflags. pt 1
SAVE_CFLAGS="x${CFLAGS}"

AC_PROG_CC
AC_PROG_CXX
AC_PROG_OBJC
AC_PROG_OBJCXX
AC_PROG_LIBTOOL

AM_PROG_CC_C_O
PKG_PROG_PKG_CONFIG

m4_ifdef([AC_PROG_OBJC],
   [AC_PROG_OBJC
    _AM_DEPENDENCIES(OBJC)],
   [AC_CHECK_TOOL([OBJC], [gcc])
    AC_SUBST([OBJC])
    AC_SUBST([OBJCFLAGS])
   ])

# check where makedev is defined
AC_HEADER_MAJOR

#output
AC_CONFIG_FILES([Makefile]
                [include/Makefile]
                [lib/Makefile]
               )

AC_OUTPUT([libavaudiosink.pc])
