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

#ifndef _SOURCES_IMPL_FILE_H
#define _SOURCES_IMPL_FILE_H

#include <sndfile.h>
#include <sigutils/types.h>

/* File sources are accessed through a soundfile handle */

struct suscan_source_config;

struct suscan_source_file {
  SNDFILE *sf;
  SF_INFO sf_info;
  struct suscan_source_config *config;

  SUBOOL iq_file;
  SUBOOL force_eos;
  SUBOOL looped;
  SUFLOAT  samp_rate;
  SUSCOUNT total_samples;
  SUSCOUNT seek_request;
};

#endif /* _SOURCES_IMPL_FILE_H */
