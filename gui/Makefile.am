AM_CPPFLAGS = -I. -I../util

noinst_LTLIBRARIES = libgui.la

libgui_la_CFLAGS = -I. -I../ctk  -I../src -ggdb -DPKGDATADIR=\"$(pkgdatadir)\" \
	-rdynamic \
	@sigutils_CFLAGS@ \
	@gtk3_CFLAGS@ \
	@GLOBAL_CFLAGS@

libgui_la_LDFLAGS = @GLOBAL_LDFLAGS@

libgui_la_SOURCES = async.c gui.c gui.h main.c spectrum.c


