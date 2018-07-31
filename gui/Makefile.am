AM_CPPFLAGS = -I. -I../util
AUTOMAKE_OPTIONS = subdir-objects

noinst_LTLIBRARIES = libgui.la

libgui_la_CFLAGS = -I. -I..  -I../src -ggdb -DPKGDATADIR=\"$(pkgdatadir)\" \
	-Imain -Icustom -Iinspector -Isymsrc \
	-rdynamic 				\
	@sigutils_CFLAGS@ \
	@gtk3_CFLAGS@ 		\
	@GLOBAL_CFLAGS@ 	\
	@soapysdr_CFLAGS@

libgui_la_LDFLAGS = @GLOBAL_LDFLAGS@

libgui_la_SOURCES = \
  cfgui.c \
  custom/constellation.c \
  custom/constellation.h \
  custom/gradient.h \
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
  gui.c \
  init.c \
  inspector/callbacks.c \
  inspector/estimatorui.c \
  inspector/estimatorui.h \
  inspector/inspector.c \
  inspector/inspector.h \
  inspector/modemctl.c \
  inspector/modemctl.h \
  inspector/serialize.c \
  main/async.c \
  main/build.c \
  main/callbacks.c \
  main/common.c \
  main/gui.h \
  main/inspector.c \
  main/log.c \
  main/main.c \
  main/params.c \
  main/profile.c \
  main/settings.c \
  main/symtool.c \
  modemctls/afc.c \
  modemctls/agc.c \
  modemctls/clock.c \
  modemctls/equalizer.c \
  modemctls/fsk.c \
  modemctls/mf.c \
  profile/build.c \
  profile/callbacks.c \
  profile/gain.c \
  profile/profile.c \
  profile/profile.h \
  symsrc/codec.c \
  symsrc/codec.h \
  symsrc/symsrc.c \
  symsrc/symsrc.h \
  symsrc/symtool.c \
  symsrc/symtool.h
