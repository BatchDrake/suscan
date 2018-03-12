AM_CPPFLAGS = -I. -I../util -I..
AUTOMAKE_OPTIONS = subdir-objects

noinst_LTLIBRARIES = libanalyzer.la

libanalyzer_la_CFLAGS = -I. -ggdb @sigutils_CFLAGS@ @bladeRF_CFLAGS@ \
  @hackRF_CFLAGS@ @analyzer_CFLAGS@

libanalyzer_la_SOURCES = sources/file.c sources/bladerf.c mq.c msg.c msg.h \
	source.c analyzer.c source.h xsig.h mq.h worker.c worker.h analyzer.h \
	sources/bladerf.h inspector.c sources/alsa.c sources/alsa.h \
	sources/hack_rf.h sources/hack_rf.c inspsched.c throttle.h inspector.h \
	insp-server.c client.c throttle.c symbuf.c symbuf.h inspsched.h \
	estimators/fac.c estimators/nonlinear.c estimator.c estimator.h \
	spectsrc.c spectsrc.h spectsrcs/psd.c spectsrcs/cyclo.c spectsrcs/exp-2.c \
	spectsrcs/exp-4.c spectsrcs/exp-8.c