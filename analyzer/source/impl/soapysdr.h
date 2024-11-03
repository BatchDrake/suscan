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
#include <SoapySDR/Device.h>
#include <SoapySDR/Formats.h>
#include <SoapySDR/Version.h>

/* SDR sources are accessed through SoapySDR */

struct suscan_source_config;
struct suscan_source;

struct suscan_source_soapysdr {
  struct suscan_source_config *config;
  struct suscan_source        *source;

  SoapySDRKwargs  *sdr_args;
  SoapySDRDevice  *sdr;
  SoapySDRStream  *rx_stream;
  SoapySDRArgInfo *settings;
  size_t           settings_count;
  SoapySDRArgInfo *stream_args;
  size_t           stream_args_count;
  char           **clock_sources;
  size_t           clock_sources_count;
  
  size_t chan_array[1];
  SUFLOAT samp_rate; /* Actual sample rate */
  size_t mtu;

  /* To prevent source from looping forever */
  SUBOOL force_eos;
  SUBOOL have_dc;
};

#endif /* _SOURCES_IMPL_SOAPYSDR_H */
