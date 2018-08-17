AM_CPPFLAGS = -I. -I

noinst_LTLIBRARIES = libutil.la

libutil_la_CFLAGS = -I. -ggdb @sigutils_CFLAGS@ @xml2_CFLAGS@ \
 -DPKGDATADIR=\"$(pkgdatadir)\" 

libutil_la_SOURCES = object.c object.h serialize.c deserialize.c util.c util.h \
	confdb.c confdb.h cfg.c cfg.h 


