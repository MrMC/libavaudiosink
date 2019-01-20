lib_LTLIBRARIES = libavaudiosink.la

#stupid autotools :)
libavaudiosink_la_LDFLAGS = \
	-Wl,-framework,Foundation \
	-Wl,-framework,AVFoundation \
	-Wl,-framework,CoreMedia \
	-ObjC \
	-lstdc++

libavaudiosink_la_CFLAGS = -xobjective-c++ -fobjc-arc

libavaudiosink_la_CFLAGS += -I$(abs_top_srcdir)/include \
		      -I$(abs_top_srcdir)/include/avaudiosink \
		      "-D_U_=__attribute__((unused))"

libavaudiosink_la_SOURCES = libavaudiosink.c

SOCURRENT=1
SOREVISION=0
SOAGE=0
libavaudiosink_la_LDFLAGS += \
	-version-info $(SOCURRENT):$(SOREVISION):$(SOAGE) -bindir $(bindir) \
	-no-undefined -export-symbols ${srcdir}/libavaudiosink.syms

dist_noinst_DATA = libavaudiosink.syms
