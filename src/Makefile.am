AM_CPPFLAGS = -Wextra -Wall -O3
LDADD = libscalpel.la
LDFLAGS += -static

lib_LTLIBRARIES = libscalpel.la
libscalpel_la_SOURCES = base_name.cpp input_reader.cpp scalpel.cpp \
    base_name.h input_reader.h scalpel.h \
    dig.cpp files.cpp syncqueue.cpp \
    common.h export.h prioque.h syncqueue.h types.h \
    helpers.cpp prioque.cpp

bin_PROGRAMS = libscalpel_test
libscalpel_test_SOURCES = libscalpel_test.cpp

