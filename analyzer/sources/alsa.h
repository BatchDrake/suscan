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

#ifndef _ANALYZER_SOURCES_ALSA_H
#define _ANALYZER_SOURCES_ALSA_H

#include <sigutils/sigutils.h>
#include <alsa/asoundlib.h>

struct alsa_params {
  const char *device;
  SUSCOUNT samp_rate;
  SUSCOUNT fc;
  SUBOOL dc_remove;
};

#define ALSA_INTEGER_BUFFER_SIZE 2048

#define alsa_params_INITIALIZER {"default", 44100, 0}

struct alsa_state {
  snd_pcm_t *handle;
  uint64_t samp_rate;
  uint64_t fc;
  int16_t buffer[ALSA_INTEGER_BUFFER_SIZE];
  SUCOMPLEX last;
  SUBOOL dc_remove;
};

SUBOOL suscan_alsa_source_init(void);

#endif /* _ANALYZER_SOURCES_ALSA_H */

