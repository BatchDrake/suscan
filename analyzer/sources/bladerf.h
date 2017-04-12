/*

  Copyright (C) 2017 Gonzalo José Carracedo Carballal

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation, either version 3 of the
  License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this program.  If not, see
  <http://www.gnu.org/licenses/>

*/


#ifndef _ANALYZER_SOURCES_BLADERF_H
#define _ANALYZER_SOURCES_BLADERF_H

#include <sigutils/sigutils.h>
#include <config.h>

# ifdef HAVE_BLADERF

#include <libbladeRF.h>

struct bladeRF_params {
  const char *serial;
  SUSCOUNT samp_rate;
  unsigned long fc;
  int    vga1;
  int    vga2;
  SUBOOL lna; /* Enable XB 300 LNA */
  int    lnagain; /* XB 300 gain */
  SUSCOUNT bufsiz; /* Buffer size */
};

#define sigutils_bladeRF_params_INITIALIZER     \
{                                               \
  NULL, /* serial */                            \
  250000, /* samp_rate */                       \
  1545346100, /* fc */                          \
  30, /* vga1 */                                \
  3, /* vga2 */                                 \
  SU_TRUE, /* lna */                            \
  BLADERF_LNA_GAIN_MAX, /* lnagain */           \
  4096, /* bufsiz */                            \
}

struct bladeRF_state {
  struct bladeRF_params params;
  struct bladerf *dev;
  uint64_t samp_rate; /* Actual sample rate */
  uint64_t fc; /* Actual frequency */
  int16_t *buffer; /* Must be SIGNED! */
};


# endif /* HAVE_BLADERF */

SUBOOL suscan_bladeRF_source_init(void);

#endif /* _ANALYZER_SOURCES_BLADERF_H */
