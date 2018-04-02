AM_CPPFLAGS = -I. -I../util -I..
AUTOMAKE_OPTIONS = subdir-objects

noinst_LTLIBRARIES = libanalyzer.la

libanalyzer_la_CFLAGS = -I. -ggdb @sigutils_CFLAGS@ @bladeRF_CFLAGS@ \
  @hackRF_CFLAGS@ @analyzer_CFLAGS@

libanalyzer_la_SOURCES = \
    analyzer.c \
    analyzer.h \
    client.c \
    estimator.c \
    estimator.h \
    estimators/fac.c \
    estimators/nonlinear.c \
    inspector/impl/fsk.c \
    inspector/impl/psk.c \
    inspector/inspector.c \
    inspector/inspector.h \
    inspector/interface.c \
    inspector/interface.h \
    inspector/params.c \
    inspector/params.h \
    inspsched.c \
    inspsched.h \
    insp-server.c \
    mq.c \
    mq.h \
    msg.c \
    msg.h \
    source.c \
    source.h \
    sources/alsa.c \
    sources/alsa.h \
    sources/bladerf.c \
    sources/bladerf.h \
    sources/file.c \
    sources/hack_rf.c \
    sources/hack_rf.h \
    spectsrc.c \
    spectsrc.h \
    spectsrcs/cyclo.c \
    spectsrcs/exp-2.c \
    spectsrcs/exp-4.c \
    spectsrcs/exp-8.c \
    spectsrcs/psd.c \
    symbuf.c \
    symbuf.h \
    throttle.c \
    throttle.h \
    worker.c \
    worker.h \
    xsig.h