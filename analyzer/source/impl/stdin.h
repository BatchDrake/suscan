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

#ifndef _SOURCES_IMPL_STDIN_H
#define _SOURCES_IMPL_STDIN_H

#include <stdio.h>
#include <sigutils/types.h>
#include <sys/poll.h>

/* File sources are accessed through a soundfile handle */

#define SUSCAN_SOURCE_STDIN_PREALLOC 1024

struct suscan_source_config;

enum suscan_source_stdin_format {
  SUSCAN_SOURCE_STDIN_FORMAT_FLOAT32,
  SUSCAN_SOURCE_STDIN_FORMAT_UNSIGNED8,
  SUSCAN_SOURCE_STDIN_FORMAT_SIGNED8,
  SUSCAN_SOURCE_STDIN_FORMAT_SIGNED16,
};

struct suscan_source_stdin;

typedef SUBOOL (*suscan_source_stdin_converter_cb_t) (
  struct suscan_source_stdin *self,
  SUCOMPLEX *output);

struct suscan_source_stdin {
  struct suscan_source_config *config;
  suscan_source_stdin_converter_cb_t converter;
  SUBOOL   realtime;
  SUSCOUNT total_samples;
  SUSCOUNT sample_size;
  union {
    void    *read_buffer;
    uint8_t *read_bytes;
  };

  SUSCOUNT read_size;  /* In samples */
  SUSCOUNT read_alloc; /* In samples*/
  SUSCOUNT read_ptr;   /* In bytes */

  struct pollfd fds[2];
  int      cancelfd[2];
  SUBOOL   have_cancelfd;
  SUBOOL   cancelled;
  SUFLOAT  samp_rate;
};

#endif /* _SOURCES_IMPL_STDIN_H */
