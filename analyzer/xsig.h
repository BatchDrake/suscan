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

#ifndef _XSIG_H
#define _XSIG_H

#include <sndfile.h>
#include <sigutils/sigutils.h>

/* Extensible signal source object */
struct xsig_source;

struct xsig_source_params {
  SUBOOL raw_iq;
  unsigned int samp_rate;
  const char *file;
  SUSCOUNT window_size;
  uint64_t fc;
  void *private;
  void (*onacquire) (struct xsig_source *source, void *private);
};

struct xsig_source {
  struct xsig_source_params params;
  SF_INFO info;
  uint64_t samp_rate;
  uint64_t fc;
  SNDFILE *sf;

  union {
    SUFLOAT *as_real;
    SUCOMPLEX *as_complex;
  };

  SUSCOUNT avail;
};

void xsig_source_destroy(struct xsig_source *source);
struct xsig_source *xsig_source_new(const struct xsig_source_params *params);
SUBOOL xsig_source_acquire(struct xsig_source *source);
su_block_t *xsig_source_create_block(const struct xsig_source_params *params);

SUBOOL suscan_wav_source_init(void);
SUBOOL suscan_iqfile_source_init(void);

#endif /* _XSIG_H */
