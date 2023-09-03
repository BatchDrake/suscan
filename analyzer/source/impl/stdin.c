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

#include "stdin.h"
#include <analyzer/source.h>
#include <util/hashlist.h>
#include <util/cfg.h>
#include <sigutils/util/compat-time.h>
#include <sigutils/util/compat-unistd.h>

#define STDIN_DATA_CONVERTER_FUNC(format)               \
  JOIN(suscan_source_read_, format)
#define STDIN_DATA_CONVERTER(format)                    \
  SUPRIVATE SUBOOL STDIN_DATA_CONVERTER_FUNC(format) (  \
  struct suscan_source_stdin *self,                     \
  SUCOMPLEX *data)

struct suscan_source_stdin_conv_info {
  suscan_source_stdin_converter_cb_t converter;
  SUSCOUNT                           sample_size;
};

SUPRIVATE hashlist_t *g_stdin_converters;

/* TODO: Use Volk */
STDIN_DATA_CONVERTER(complex_float32)
{
  memcpy(data, self->read_buffer, self->read_size * sizeof(SUCOMPLEX));
  return SU_TRUE;
}

STDIN_DATA_CONVERTER(float32)
{
  SUSCOUNT i;
  const SUFLOAT *as_float = (const SUFLOAT *) self->read_buffer;

  for (i = 0; i < self->read_size; ++i)
    data[i] = as_float[i];

  return SU_TRUE;
}

STDIN_DATA_CONVERTER(complex_unsigned8)
{
  SUSCOUNT i;
  const uint8_t *as_bytes = (const uint8_t *) self->read_buffer;

  for (i = 0; i < self->read_size; ++i)
    data[i] = as_bytes[2 * i] / 255. + I * as_bytes[2 * i + 1] / 255.;

  return SU_TRUE;
}

STDIN_DATA_CONVERTER(unsigned8)
{
  SUSCOUNT i;
  const uint8_t *as_bytes = (const uint8_t *) self->read_buffer;

  for (i = 0; i < self->read_size; ++i)
    data[i] = as_bytes[i] / 255.;

  return SU_TRUE;
}

STDIN_DATA_CONVERTER(complex_signed8)
{
  SUSCOUNT i;
  const int8_t *as_int8 = (const int8_t *) self->read_buffer;

  for (i = 0; i < self->read_size; ++i)
    data[i] = as_int8[2 * i] / 255. + I * as_int8[2 * i + 1] / 255.;

  return SU_TRUE;
}

STDIN_DATA_CONVERTER(signed8)
{
  SUSCOUNT i;
  const int8_t *as_int8 = (const int8_t *) self->read_buffer;

  for (i = 0; i < self->read_size; ++i)
    data[i] = as_int8[i] / 255.;

  return SU_TRUE;
}

STDIN_DATA_CONVERTER(complex_signed16)
{
  SUSCOUNT i;
  const int16_t *as_int16 = (const int16_t *) self->read_buffer;

  for (i = 0; i < self->read_size; ++i)
    data[i] = as_int16[2 * i] / 65535. + I * as_int16[2 * i + 1] / 65535.;

  return SU_TRUE;
}

STDIN_DATA_CONVERTER(signed16)
{
  SUSCOUNT i;
  const int16_t *as_int16 = (const int16_t *) self->read_buffer;

  for (i = 0; i < self->read_size; ++i)
    data[i] = as_int16[i] / 65535.;

  return SU_TRUE;
}

/****************************** Implementation ********************************/
SUPRIVATE void
suscan_source_stdin_close(void *ptr)
{
  struct suscan_source_stdin *self = (struct suscan_source_stdin *) ptr;

  if (self->have_cancelfd) {
  close(self->cancelfd[0]);
    close(self->cancelfd[1]);
  }

  if (self->read_buffer != NULL)
    free(self->read_buffer);
  
  free(self);
}

SUPRIVATE SUBOOL
suscan_source_stdin_is_realtime(const suscan_source_config_t *self)
{
  const char *rt;

  rt = suscan_source_config_get_param(self, "realtime");

  return suscan_config_str_to_bool(rt, SU_FALSE);
}

SUPRIVATE SUBOOL
suscan_source_stdin_set_converter(struct suscan_source_stdin *self)
{
  const char *format = suscan_source_config_get_param(
    self->config,
    "format");
  struct suscan_source_stdin_conv_info *info;

  if (format == NULL) {
    SU_ERROR("stdin: input data format not specified\n");
    return SU_FALSE;
  }

  info = hashlist_get(g_stdin_converters, format);
  if (info == NULL) {
    SU_ERROR("stdin: sample format `%s' unknown\n", format);
    return SU_FALSE;
  }

  self->converter   = info->converter;
  self->sample_size = info->sample_size;

  return SU_TRUE;
}

SUPRIVATE void *
suscan_source_stdin_open(
  suscan_source_t *source,
  suscan_source_config_t *config,
  struct suscan_source_info *info)
{
  struct suscan_source_stdin *new = NULL;

  SU_ALLOCATE_FAIL(new, struct suscan_source_stdin);
  new->config = config;
  
  SU_TRYC_FAIL(pipe(new->cancelfd));
  new->have_cancelfd  = SU_TRUE;

  new->fds[0].fd      = 0;
  new->fds[0].events  = POLLIN | POLLERR | POLLHUP;
  new->fds[1].fd      = new->cancelfd[0];
  new->fds[1].events  = POLLIN;

  /* Initialize source info */
  suscan_source_info_init(info);
  info->permissions         = SUSCAN_ANALYZER_ALL_FILE_PERMISSIONS;
  info->permissions        &= ~SUSCAN_ANALYZER_PERM_SEEK;

  info->source_samp_rate    = config->samp_rate;
  info->effective_samp_rate = config->samp_rate;
  info->measured_samp_rate  = config->samp_rate;
  new->samp_rate            = (SUFLOAT) info->source_samp_rate;
  info->source_start        = config->start_time;

  info->realtime            = suscan_source_stdin_is_realtime(config);
  new->realtime             = info->realtime;

  SU_TRY_FAIL(suscan_source_stdin_set_converter(new));

  return new;

fail:
  if (new != NULL)
    suscan_source_stdin_close(new);

  return NULL;
}

SUPRIVATE SUBOOL
suscan_source_stdin_start(void *self)
{
  return SU_TRUE;
}

SUPRIVATE SUSDIFF
suscan_source_stdin_read(
  void *userdata,
  SUCOMPLEX *buf,
  SUSCOUNT max)
{
  struct suscan_source_stdin *self = (struct suscan_source_stdin *) userdata;
  
  SUSCOUNT bufsize = max * self->sample_size;
  SUSCOUNT avail;
  SUSCOUNT complete_ptr;
  ssize_t ret;
  void *tmp;

  if (self->cancelled)
    return 0;
  
  if (bufsize > self->read_alloc) {
    tmp = realloc(self->read_buffer, bufsize);
    if (tmp == NULL) {
      SU_ERROR("Failed to reallocate read buffer to %d bytes\n", bufsize);
      return -1;
    }

    self->read_buffer = tmp;
    self->read_alloc  = bufsize;
  }

  self->read_size = 0;

  while (self->read_size == 0) {
    avail = self->read_alloc - self->read_ptr;

    /* Polled read */
    if (poll(self->fds, 2, -1) == -1) {
      SU_ERROR("poll() error: %s\n", strerror(errno));
      return -1;
    }

    if (self->fds[1].revents & POLLIN) {
      self->cancelled = SU_TRUE;
      return 0;
    }

    ret = read(0, self->read_bytes + self->read_ptr, avail);

    if (ret == 0) {
      SU_INFO("EOF while reading %d bytes from standard input, closing stream.\n", avail);
      return 0;
    }

    if (ret < 0) {
      SU_ERROR("Error while reading from stdin: %s\n", strerror(errno));
      return -1;
    }

    self->read_ptr += ret;

    if (self->read_ptr >= self->sample_size) {
      self->read_size = self->read_ptr / self->sample_size;

      /* Run conversion! Read size is now different from 0 */
      if (!(self->converter)(self, buf)) {
        SU_ERROR("Data conversion error\n");
        return -1;
      }

      complete_ptr = self->read_size * self->sample_size;
      if (complete_ptr < self->read_ptr) {
        /* Move the half-read sample to the beginning */
        memcpy(
          self->read_buffer,
          self->read_bytes + complete_ptr,
          self->read_ptr - complete_ptr);

        self->read_ptr = self->read_ptr - complete_ptr;
      } else {
        self->read_ptr = 0;
      }
    }
  }

  return self->read_size;
}

SUPRIVATE void
suscan_source_stdin_get_time(void *userdata, struct timeval *tv)
{
  struct suscan_source_stdin *self = (struct suscan_source_stdin *) userdata;
  struct timeval elapsed;

  if (self->realtime) {
    gettimeofday(tv, NULL);
  } else {
    SUSCOUNT samp_count = self->total_samples;
    SUFLOAT samp_rate = self->samp_rate;

    elapsed.tv_sec  = samp_count / samp_rate;
    elapsed.tv_usec = 
      (1000000 
        * (samp_count - elapsed.tv_sec * samp_rate))
        / samp_rate;

    timeradd(&self->config->start_time, &elapsed, tv);
  }
}

SUPRIVATE SUBOOL
suscan_source_stdin_cancel(void *userdata)
{
  struct suscan_source_stdin *self = (struct suscan_source_stdin *) userdata;
  char b = 1;

  self->cancelled = SU_TRUE;
  if (write(self->cancelfd[1], &b, 1) < 1) {
    SU_ERROR("Failed to send cancel signal: %s\n", strerror(errno));
    return SU_FALSE;
  }

  return SU_TRUE;
}

SUPRIVATE struct suscan_source_interface g_stdin_source =
{
  .name            = "stdin",
  .desc            = "Standard input",
  .realtime        = SU_FALSE,

  .open            = suscan_source_stdin_open,
  .close           = suscan_source_stdin_close,
  .start           = suscan_source_stdin_start,
  .cancel          = suscan_source_stdin_cancel,
  .read            = suscan_source_stdin_read,
  .get_time        = suscan_source_stdin_get_time,
  .is_real_time    = suscan_source_stdin_is_realtime,

  /* Unset members */
  .set_frequency   = NULL,
  .set_gain        = NULL,
  .set_antenna     = NULL,
  .set_bandwidth   = NULL,
  .set_ppm         = NULL,
  .set_dc_remove   = NULL,
  .set_agc         = NULL,
  .get_freq_limits = NULL,
  .estimate_size   = NULL,
  .seek            = NULL,
  .max_size        = NULL,
};
  
SUBOOL
suscan_soruce_stdin_register_converter(
  const char *name,
  suscan_source_stdin_converter_cb_t func,
  SUSCOUNT size)
{
  struct suscan_source_stdin_conv_info *info;
  SUBOOL ok = SU_FALSE;

  SU_ALLOCATE(info, struct suscan_source_stdin_conv_info);

  info->converter   = func;
  info->sample_size = size;

  SU_TRY(hashlist_set(g_stdin_converters, name, info));

  ok = SU_TRUE;

done:
  if (!ok)
    free(info);
  
  return ok;
}

#define STDIN_REGISTER_CONVERTER(format, size)  \
  SU_TRY(                                       \
    suscan_soruce_stdin_register_converter(     \
    STRINGIFY(format),                          \
    STDIN_DATA_CONVERTER_FUNC(format),          \
    size))

SUBOOL
suscan_source_register_stdin(void)
{
  int ndx;
  SUBOOL ok = SU_FALSE;

  SU_MAKE(g_stdin_converters, hashlist);

  STDIN_REGISTER_CONVERTER(complex_float32,     8);
  STDIN_REGISTER_CONVERTER(float32,             4);
  STDIN_REGISTER_CONVERTER(complex_unsigned8,   2);
  STDIN_REGISTER_CONVERTER(unsigned8,           1);
  STDIN_REGISTER_CONVERTER(complex_signed8,     2);
  STDIN_REGISTER_CONVERTER(signed8,             1);
  STDIN_REGISTER_CONVERTER(complex_signed16,    4);
  STDIN_REGISTER_CONVERTER(signed16,            2);

  SU_TRYC(ndx = suscan_source_register(&g_stdin_source));

  ok = SU_TRUE;

done:
  return ok;
}
