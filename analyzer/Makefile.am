AM_CPPFLAGS = -I. -I../util

noinst_LTLIBRARIES = libanalyzer.la

libanalyzer_la_CFLAGS = -I. -ggdb @sigutils_CFLAGS@

libanalyzer_la_SOURCES = file_sources.c mq.c msg.c msg.h source.c \
	analyzer.c source.h xsig.h mq.h worker.c worker.h analyzer.h
	