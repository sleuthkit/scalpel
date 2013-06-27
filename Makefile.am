AM_CPPFLAGS = -Wextra -Wall -O3
LDADD = src/libscalpel.la
LDFLAGS += -static

if X_JNI
  JNI=jni
endif
SUBDIRS = src man $(JNI)
ACLOCAL_AMFLAGS = -I m4

bin_PROGRAMS = scalpel
scalpel_SOURCES = src/scalpel_exec.cpp

EXTRA_DIST = README Changelog scalpel.conf  \
             pthreads-win32 tre-0.7.5-win32 pthreadGC2.dll Makefile.win libtre-4.dll man/scalpel.1
