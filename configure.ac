#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.67])
AC_INIT(scalpel, 2.0, scalpel@digdeeply.com)
LT_INIT
AM_INIT_AUTOMAKE(scalpel, 2.0)
AC_CONFIG_SRCDIR([pthreads-win32/pthread.h])
AC_CONFIG_MACRO_DIR([m4])
#AC_CONFIG_HEADERS([config.h])
AC_PROG_INSTALL
AC_SYS_LARGEFILE
m4_include([m4/ax_jni_include_dir.m4])
m4_include([m4/ac_prog_javac_works.m4])
m4_include([m4/ac_prog_javac.m4])

# Checks for programs.
AC_PROG_CXX
AC_PROG_CPP

# Checks for libraries.
AC_CHECK_LIB([m], [fabs])
AC_CHECK_LIB([pthread], [pthread_create], [], [AC_MSG_ERROR(Scalpel requires the pthreads library.)])
AC_CHECK_LIB([tre], [regcomp], [], [AC_MSG_ERROR(Scalpel requires libtre and libtre-dev. See http://laurikari.net/tre/.)])

# Checks for header files.
AC_CHECK_HEADERS([fcntl.h limits.h stddef.h stdlib.h string.h sys/ioctl.h sys/mount.h sys/param.h sys/time.h sys/timeb.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_INLINE
AC_TYPE_MODE_T
AC_TYPE_OFF_T
AC_TYPE_PID_T
AC_TYPE_SIZE_T

# Checks for library functions.
AC_FUNC_FSEEKO
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_CHECK_FUNCS([bzero gettimeofday mkdir realpath regcomp strchr strdup strerror strncasecmp strtoul strtoull])

# Test for java/jni so that we can compile the java bindings
AC_PROG_JAVAC
if test "x$JAVAC" != x; then
    AX_JNI_INCLUDE_DIR
    for JNI_INCLUDE_DIR in $JNI_INCLUDE_DIRS
    do
        JNI_CPPFLAGS="$JNI_CPPFLAGS -I$JNI_INCLUDE_DIR"
    done
    # Export the paths so that the makefile gets them
    AC_SUBST(JNI_CPPFLAGS, $JNI_CPPFLAGS)
fi
AM_CONDITIONAL([X_JNI],[test "x$JNI_CPPFLAGS" != x])

#AC_CONFIG_SUBDIRS([tre-0.7.5-win32])
AC_OUTPUT(Makefile src/Makefile man/Makefile jni/Makefile)
