/*

  Copyright (C) 2023 Gonzalo José Carracedo Carballal

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

#ifndef _SOURCES_IMPL_SOAPYSDR_H
#define _SOURCES_IMPL_SOAPYSDR_H

#include <sndfile.h>
#include <sigutils/types.h>
#include <sigutils/ncqo.h>
#include <analyzer/throttle.h>
#include <SoapySDR/Device.h>
#include <SoapySDR/Formats.h>
#include <SoapySDR/Version.h>

/* SDR sources are accessed through SoapySDR */

struct suscan_source_config;
struct suscan_source;

struct suscan_source_tonegen {
  struct suscan_source_config *config;
  struct suscan_source        *source;
  
  suscan_throttle_t throttle;
  su_ncqo_t         tone;

  SUFLOAT   samp_rate;
  SUFLOAT   noise_amplitude;
  SUFLOAT   signal_amplitude;
  SUBOOL    out_of_band;
  
  SUBOOL    force_eos;
  SUFREQ    init_freq;
  SUFREQ    curr_freq;
};

#endif /* _SOURCES_IMPL_SOAPYSDR_H */
