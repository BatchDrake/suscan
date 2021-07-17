/*

  Copyright (C) 2017 Gonzalo José Carracedo Carballal

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation, version 3.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this program.  If not, see
  <http://www.gnu.org/licenses/>

*/


#ifndef _ANALYZER_SOURCES_HACKRF_H
#define _ANALYZER_SOURCES_HACKRF_H

#include <sigutils/sigutils.h>
#include <config.h>

# ifdef HAVE_HACKRF

#include <libhackrf/hackrf.h>

#define HACKRF_STREAM_SIZE (1024 * 1024)

struct hackRF_params {
  const char *serial;
  SUSCOUNT samp_rate;    /* Sample rate */
  unsigned long fc;      /* Center frequency */
  SUBOOL amp_enable;     /* Enable antenna amplifier */
  unsigned int vga_gain; /* VGA gain (baseband) */
  unsigned int lna_gain; /* LNA gain (IF) */
  SUBOOL bias;           /* Bias tee */
  SUSCOUNT bufsiz;       /* Buffer size */
};

#define sigutils_hackRF_params_INITIALIZER     \
{                                              \
  NULL, /* serial */                           \
  250000, /* samp_rate */                      \
  1545346100, /* fc */                         \
  SU_FALSE, /* amp_enable */                   \
  30, /* vga_gain */                           \
  0,  /* lna_gain */                           \
  SU_FALSE, /* bias tee */                     \
  HACKRF_STREAM_SIZE, /* bufsiz */             \
}

struct hackRF_state {
  struct hackRF_params params;
  struct hackrf_device *dev;
  uint64_t samp_rate; /* Actual sample rate */
  uint64_t fc; /* Actual frequency */
  pthread_mutex_t lock;
  pthread_cond_t cond;
  su_stream_t stream;
  SUBOOL rx_started;
  SUBOOL toggle_iq;
  SUCOMPLEX samp;
};


# endif /* HAVE_HACKRF */

SUBOOL suscan_hackRF_source_init(void);

#endif /* _ANALYZER_SOURCES_HACKRF_H */
