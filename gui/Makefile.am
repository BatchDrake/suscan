AM_CPPFLAGS = -I. -I../util
AUTOMAKE_OPTIONS = subdir-objects

noinst_LTLIBRARIES = libgui.la

libgui_la_CFLAGS = -I. -I..  -I../src -ggdb -DPKGDATADIR=\"$(pkgdatadir)\" \
	-Imain -Icustom \
	-rdynamic 				\
	@sigutils_CFLAGS@ \
	@gtk3_CFLAGS@ 		\
	@GLOBAL_CFLAGS@ 	\
	@soapysdr_CFLAGS@

libgui_la_LDFLAGS = @GLOBAL_LDFLAGS@

libgui_la_SOURCES = \
  callbacks.c \
  cfgui.c \
  codec.c \
  codec.h \
  custom/constellation.c \
  custom/constellation.h \
  custom/histogram.c \
  custom/histogram.h \
  custom/lcd.c \
  custom/lcd.h \
  custom/spectrum.c \
  custom/spectrum.h \
  custom/sugtk.h \
  custom/symview.c \
  custom/symview.h \
  custom/transmtx.c \
  custom/transmtx.h \
  custom/waveform.c \
  custom/waveform.h \
  estimatorui.c \
  estimatorui.h \
  gradient.h \
  gui.c \
  inspector.c \
  inspector.h \
  log.c \
  main/async.c \
  main/build.c \
  main/common.c \
  main/gui.h \
  main/inspector.c \
  main/main.c \
  main/params.c \
  main/profile.c \
  main/settings.c \
  main/symtool.c \
  modemctl.c \
  modemctl.h \
  modemctls/afc.c \
  modemctls/agc.c \
  modemctls/clock.c \
  modemctls/equalizer.c \
  modemctls/fsk.c \
  modemctls/mf.c \
  profile.c \
  profile.h \
  symsrc.c \
  symsrc.h \
  symtool.c \
  symtool.h
  