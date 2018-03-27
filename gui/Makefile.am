AM_CPPFLAGS = -I. -I../util

noinst_LTLIBRARIES = libgui.la

libgui_la_CFLAGS = -I. -I..  -I../src -ggdb -DPKGDATADIR=\"$(pkgdatadir)\" \
	-rdynamic \
	@sigutils_CFLAGS@ \
	@gtk3_CFLAGS@ \
	@GLOBAL_CFLAGS@

libgui_la_LDFLAGS = @GLOBAL_LDFLAGS@

libgui_la_SOURCES = async.c cfgui.c constellation.c gui.c gui.h inspector.c    \
	log.c main.c spectrum.c recent.c gradient.h settings.c symview.c symview.h   \
	transmtx.c transmtx.h codec.c inspector.h spectrum.h constellation.h codec.h \
	symsrc.c symsrc.h symtool.c symtool.h modemctl.c modemctl.h modemctls/agc.c  \
	modemctls/afc.c modemctls/mf.c modemctls/equalizer.c modemctls/clock.c       \
	estimatorui.c estimatorui.h lcd.c lcd.h waveform.c waveform.h histogram.c    \
	histogram.h