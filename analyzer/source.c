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

#define _DEFAULT_SOURCE

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <libgen.h>
#include <inttypes.h>

#define SU_LOG_DOMAIN "source"

#include "source.h"
#include "compat.h"
#include "discovery.h"

#include <sigutils/taps.h>
#include <sigutils/specttuner.h>
#include <fcntl.h>
#include <unistd.h>
#include <sigutils/util/compat-time.h>
#include <confdb.h>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  if defined(interface)
#    undef interface
#  endif /* interface */
#endif /* _WIN32 */

/* Come on */
#ifdef bool
#  undef bool
#endif

/********************************** Gain info *********************************/
void
suscan_source_gain_info_destroy(struct suscan_source_gain_info *self)
{
  if (self->name != NULL)
    free(self->name);

  free(self);
}

struct suscan_source_gain_info *
suscan_source_gain_info_dup(
    const struct suscan_source_gain_info *old)
{
  struct suscan_source_gain_info *new = NULL;

  SU_TRYCATCH(
      new = calloc(1, sizeof(struct suscan_source_gain_info)),
      goto fail);

  SU_TRYCATCH(new->name = strdup(old->name), goto fail);

  new->max   = old->max;
  new->min   = old->min;
  new->step  = old->step;
  new->value = old->value;

  return new;

fail:
  if (new != NULL)
    suscan_source_gain_info_destroy(new);

  return NULL;
}

struct suscan_source_gain_info *
suscan_source_gain_info_new(
    const struct suscan_source_gain_value *value)
{
  struct suscan_source_gain_info *new = NULL;

  SU_TRYCATCH(
      new = calloc(1, sizeof(struct suscan_source_gain_info)),
      goto fail);

  SU_TRYCATCH(new->name = strdup(value->desc->name), goto fail);

  new->max   = value->desc->max;
  new->min   = value->desc->min;
  new->step  = value->desc->step;
  new->value = value->val;

  return new;

fail:
  if (new != NULL)
    suscan_source_gain_info_destroy(new);

  return NULL;
}

struct suscan_source_gain_info *
suscan_source_gain_info_new_value_only(
    const char *name,
    SUFLOAT value)
{
  struct suscan_source_gain_info *new = NULL;

  SU_TRYCATCH(
      new = calloc(1, sizeof(struct suscan_source_gain_info)),
      goto fail);

  SU_TRYCATCH(new->name = strdup(name), goto fail);

  new->value = value;

  return new;

fail:
  if (new != NULL)
    suscan_source_gain_info_destroy(new);

  return NULL;
}

/* Helper methods */
SUSCAN_SERIALIZER_PROTO(suscan_source_info)
{
  SUSCAN_PACK_BOILERPLATE_START;
  unsigned int i;

  SUSCAN_PACK(uint,  self->permissions);
  SUSCAN_PACK(uint,  self->mtu);
  SUSCAN_PACK(uint,  self->source_samp_rate);
  SUSCAN_PACK(uint,  self->effective_samp_rate);
  SUSCAN_PACK(float, self->measured_samp_rate);
  SUSCAN_PACK(freq,  self->frequency);
  SUSCAN_PACK(freq,  self->freq_min);
  SUSCAN_PACK(freq,  self->freq_max);
  SUSCAN_PACK(freq,  self->lnb);
  SUSCAN_PACK(float, self->bandwidth);
  SUSCAN_PACK(float, self->ppm);
  SUSCAN_PACK(str,   self->antenna);
  SUSCAN_PACK(bool,  self->dc_remove);
  SUSCAN_PACK(bool,  self->iq_reverse);
  SUSCAN_PACK(bool,  self->agc);

  SUSCAN_PACK(bool,   self->have_qth);
  if (self->have_qth) {
    SUSCAN_PACK(double, self->qth.lat);
    SUSCAN_PACK(double, self->qth.lon);
    SUSCAN_PACK(double, self->qth.height);
  }

  SUSCAN_PACK(uint, self->source_time.tv_sec);
  SUSCAN_PACK(uint, self->source_time.tv_usec);

  SUSCAN_PACK(bool, self->seekable);
  if (self->seekable) {
    SUSCAN_PACK(uint,  self->source_start.tv_sec);
    SUSCAN_PACK(uint,  self->source_start.tv_usec);  
    SUSCAN_PACK(uint, self->source_end.tv_sec);
    SUSCAN_PACK(uint, self->source_end.tv_usec);
  }

  SU_TRYCATCH(cbor_pack_map_start(buffer, self->gain_count) == 0, goto fail);
  for (i = 0; i < self->gain_count; ++i)
    SU_TRYCATCH(
        suscan_source_gain_info_serialize(self->gain_list[i], buffer),
        goto fail);

  SU_TRYCATCH(cbor_pack_map_start(buffer, self->antenna_count) == 0, goto fail);
  for (i = 0; i < self->antenna_count; ++i)
    SUSCAN_PACK(str, self->antenna_list[i]);

  SUSCAN_PACK_BOILERPLATE_END;
}

SUSCAN_DESERIALIZER_PROTO(suscan_source_info)
{
  SUSCAN_UNPACK_BOILERPLATE_START;
  SUBOOL end_required = SU_FALSE;
  size_t i;
  uint64_t nelem = 0;
  uint64_t tv_sec = 0;
  uint32_t tv_usec = 0;

  SUSCAN_UNPACK(uint64, self->permissions);
  SUSCAN_UNPACK(uint32, self->mtu);
  SUSCAN_UNPACK(uint64, self->source_samp_rate);
  SUSCAN_UNPACK(uint64, self->effective_samp_rate);
  SUSCAN_UNPACK(float,  self->measured_samp_rate);
  SUSCAN_UNPACK(freq,   self->frequency);
  SUSCAN_UNPACK(freq,   self->freq_min);
  SUSCAN_UNPACK(freq,   self->freq_max);
  SUSCAN_UNPACK(freq,   self->lnb);
  SUSCAN_UNPACK(float,  self->bandwidth);
  SUSCAN_UNPACK(float,  self->ppm);
  SUSCAN_UNPACK(str,    self->antenna);
  SUSCAN_UNPACK(bool,   self->dc_remove);
  SUSCAN_UNPACK(bool,   self->iq_reverse);
  SUSCAN_UNPACK(bool,   self->agc);

  SUSCAN_UNPACK(bool,   self->have_qth);
  if (self->have_qth) {
    SUSCAN_UNPACK(double, self->qth.lat);
    SUSCAN_UNPACK(double, self->qth.lon);
    SUSCAN_UNPACK(double, self->qth.height);
  }

  SUSCAN_UNPACK(uint64, tv_sec);
  SUSCAN_UNPACK(uint32, tv_usec);
  self->source_time.tv_sec  = tv_sec;
  self->source_time.tv_usec = tv_usec;

  SUSCAN_UNPACK(bool, self->seekable);
  if (self->seekable) {
    SUSCAN_UNPACK(uint64, tv_sec);
    SUSCAN_UNPACK(uint32, tv_usec);  
    self->source_start.tv_sec  = tv_sec;
    self->source_start.tv_usec = tv_usec;

    SUSCAN_UNPACK(uint64, tv_sec);
    SUSCAN_UNPACK(uint32, tv_usec);
    self->source_end.tv_sec  = tv_sec;
    self->source_end.tv_usec = tv_usec;
  }

  /* Deserialize gains */
  SU_TRYCATCH(
      cbor_unpack_map_start(buffer, &nelem, &end_required) == 0,
      goto fail);
  SU_TRYCATCH(!end_required, goto fail);

  self->gain_count = (unsigned int) nelem;

  if (self->gain_count > 0) {
    SU_TRYCATCH(
        self->gain_list = calloc(
            nelem,
            sizeof (struct suscan_source_gain_info *)),
        goto fail);

    for (i = 0; i < self->gain_count; ++i) {
      SU_TRYCATCH(
          self->gain_list[i] = 
            calloc(1, sizeof (struct suscan_source_gain_info)),
          goto fail);

      SU_TRYCATCH(
          suscan_source_gain_info_deserialize(self->gain_list[i], buffer),
          goto fail);
    }
  } else {
    self->gain_list = NULL;
  }
  
  /* Deserialize antennas */
  SU_TRYCATCH(
      cbor_unpack_map_start(buffer, &nelem, &end_required) == 0,
      goto fail);
  SU_TRYCATCH(!end_required, goto fail);

  self->antenna_count = (unsigned int) nelem;

  if (self->antenna_count > 0) {
    SU_TRYCATCH(
      self->antenna_list = calloc(nelem, sizeof (char *)), 
      goto fail);

    for (i = 0; i < self->antenna_count; ++i)
      SUSCAN_UNPACK(str, self->antenna_list[i]);
  } else {
    self->antenna_list = NULL;
  }

  SUSCAN_UNPACK_BOILERPLATE_END;
}

void
suscan_source_info_init(struct suscan_source_info *self)
{
  memset(self, 0, sizeof(struct suscan_source_info));

  self->permissions = SUSCAN_ANALYZER_PERM_ALL;
}

SUBOOL
suscan_source_info_init_copy(
    struct suscan_source_info *self,
    const struct suscan_source_info *origin)
{
  struct suscan_source_gain_info *gi = NULL;
  char *dup = NULL;
  unsigned int i;
  SUBOOL ok = SU_FALSE;

  suscan_source_info_init(self);

  self->permissions         = origin->permissions;
  self->source_samp_rate    = origin->source_samp_rate;
  self->effective_samp_rate = origin->effective_samp_rate;
  self->measured_samp_rate  = origin->measured_samp_rate;
  self->frequency           = origin->frequency;
  self->freq_min            = origin->freq_min;
  self->freq_max            = origin->freq_max;
  self->lnb                 = origin->lnb;
  self->bandwidth           = origin->bandwidth;
  self->ppm                 = origin->ppm;
  self->source_time         = origin->source_time;
  self->seekable            = origin->seekable;

  if (self->seekable) {
    self->source_start = origin->source_start;
    self->source_end   = origin->source_end;
  }

  if (origin->antenna != NULL)
    SU_TRYCATCH(self->antenna = strdup(origin->antenna), goto done);

  self->dc_remove  = origin->dc_remove;
  self->iq_reverse = origin->iq_reverse;
  self->agc        = origin->agc;

  for (i = 0; i < origin->gain_count; ++i) {
    SU_TRYCATCH(
        gi = suscan_source_gain_info_dup(origin->gain_list[i]),
        goto done);

    SU_TRYCATCH(PTR_LIST_APPEND_CHECK(self->gain, gi) != -1, goto done);
    gi = NULL;
  }

  for (i = 0; i < origin->antenna_count; ++i) {
    SU_TRYCATCH(dup = strdup(origin->antenna_list[i]), goto done);
    SU_TRYCATCH(PTR_LIST_APPEND_CHECK(self->antenna, dup) != -1, goto done);
    dup = NULL;
  }

  ok = SU_TRUE;

done:
  if (gi != NULL)
    suscan_source_gain_info_destroy(gi);

  if (dup != NULL)
    free(dup);

  if (!ok)
    suscan_source_info_finalize(self);

  return ok;
}

void
suscan_source_info_finalize(struct suscan_source_info *self)
{
  unsigned int i;

  if (self->antenna != NULL)
    free(self->antenna);

  for (i = 0; i < self->gain_count; ++i)
    if (self->gain_list[i] != NULL)
      suscan_source_gain_info_destroy(self->gain_list[i]);

  if (self->gain_list != NULL)
    free(self->gain_list);

  for (i = 0; i < self->antenna_count; ++i)
    if (self->antenna_list[i] != NULL)
      free(self->antenna_list[i]);

  if (self->antenna_list != NULL)
    free(self->antenna_list);

  memset(self, 0, sizeof(struct suscan_source_info));
}

/****************************** Source API ***********************************/
void
suscan_source_destroy(suscan_source_t *source)
{
  if (source->source != NULL)
    (source->iface->close) (source->source);
  
  if (source->config != NULL)
    suscan_source_config_destroy(source->config);

  if (source->decimator != NULL)
    su_specttuner_destroy(source->decimator);

  if (source->decim_spillover != NULL)
    free(source->decim_spillover);

  if (source->read_buf != NULL)
    free(source->read_buf);

  free(source);
}

/*********************** Decimator configuration *****************************/
#define _SWAP(a, b) \
  tmp = a;          \
  a = b;            \
  b = tmp;

SUPRIVATE SUBOOL
suscan_source_decim_callback(
  const struct sigutils_specttuner_channel *channel,
   void *privdata,
   const SUCOMPLEX *data,
   SUSCOUNT size)
{
  suscan_source_t *self = privdata;
  SUSCOUNT chunk;
  SUSCOUNT curr_avail;
  SUSCOUNT spillover_avail;
  SUCOMPLEX *buftmp;
  SUSCOUNT desired_alloc = SUSCAN_SOURCE_DECIMATOR_BUFFER_SIZE;
  SUBOOL ok = SU_FALSE;
  
  while (size > 0) {
    curr_avail = self->curr_size - self->curr_ptr;
    spillover_avail = self->decim_spillover_alloc - self->decim_spillover_size;
    
    /* 
     *  Two cases here:
     *  1. curr_avail > 0: copy to the current buffer, increment pointer 
     *  2. curr_avail = 0: copy to spillover buffer, increment pointer 
     *      spillover_avail > 0:  copy until done 
     *      spillover_avail == 0: increment allocation 
     *  In all cases, decrement size appropriately
     */

    if (curr_avail > 0) {
      chunk = SU_MIN(curr_avail, size);
      memcpy(self->curr_buf + self->curr_ptr, data, chunk * sizeof(SUCOMPLEX));
      self->curr_ptr += chunk;
    } else {
      if (spillover_avail == 0) {
        if (self->decim_spillover_alloc > 0)
          desired_alloc = self->decim_spillover_alloc << 1;
        
        SU_TRY(
          buftmp = realloc(
            self->decim_spillover,
            desired_alloc * sizeof(SUCOMPLEX)));

        self->decim_spillover = buftmp;
        self->decim_spillover_alloc = desired_alloc;
        spillover_avail = self->decim_spillover_alloc - self->decim_spillover_size;
      }

      chunk = SU_MIN(spillover_avail, size);
      memcpy(
        self->decim_spillover + self->decim_spillover_size,
        data,
        chunk * sizeof(SUCOMPLEX));
      self->decim_spillover_size += chunk;
    }

    size -= chunk;
    data += chunk;
  }
  
  ok = SU_TRUE;

done:
  return ok;
}

SUPRIVATE SUBOOL
suscan_source_configure_decimation(
    suscan_source_t *self,
    int decim)
{
  int true_decim;
  SUBOOL ok = SU_FALSE;
  struct sigutils_specttuner_params params = 
    sigutils_specttuner_params_INITIALIZER;
  su_specttuner_t *new_tuner = NULL, *tmp;
  su_specttuner_channel_t *chan = NULL;
  struct sigutils_specttuner_channel_params chparams =
    sigutils_specttuner_channel_params_INITIALIZER;

  SU_TRY(decim > 0);

  true_decim = 1;
  while (true_decim < decim)
    true_decim <<= 1;

  if (true_decim > 1) {
    SU_ALLOCATE_MANY(self->read_buf, SUSCAN_SOURCE_DEFAULT_BUFSIZ, SUCOMPLEX);

    params.window_size     = SUSCAN_SOURCE_DEFAULT_BUFSIZ;
    params.early_windowing = SU_FALSE;

    SU_MAKE(new_tuner, su_specttuner, &params);

    chparams.guard    = 1;
    chparams.bw       = 2 * M_PI / true_decim * (1 - SUSCAN_SOURCE_DECIM_INNER_GUARD);
    chparams.f0       = 0;
    chparams.precise  = SU_TRUE;
    chparams.privdata = self;
    chparams.on_data  = suscan_source_decim_callback;

    SU_TRY(chan = su_specttuner_open_channel(new_tuner, &chparams));
    self->main_channel = chan;
  }

  _SWAP(new_tuner, self->decimator);
  self->decim = true_decim;
  
  ok = SU_TRUE;

done:
  if (new_tuner != NULL)
    su_specttuner_destroy(new_tuner);

  return ok;
}
#undef _SWAP

SUPRIVATE SUSCOUNT
suscan_source_feed_decimator(
    suscan_source_t *self,
    const SUCOMPLEX *data,
    SUSCOUNT len)
{
  su_specttuner_feed_bulk(self->decimator, data, len);
  return len;
}

SUSCOUNT
suscan_source_get_dc_samples(const suscan_source_t *self)
{
  SUFLOAT samp_rate = suscan_source_get_samp_rate(self);
  const char *calc_time;
  SUFLOAT int_time = 0;

  calc_time = suscan_source_config_get_param(
    self->config,
    "_suscan_dc_calc_time");
  
  if (calc_time != NULL) {
    if (sscanf(calc_time, "%f", &int_time) == 1) {
      if (int_time < 0)
        int_time = 0;
    } else {
      int_time = 0;
    }
  }

  return (SUSCOUNT) (int_time * samp_rate);
}

SUSDIFF
suscan_source_read(suscan_source_t *self, SUCOMPLEX *buffer, SUSCOUNT max)
{
  SUSDIFF got = 0;
  SUSCOUNT result, i;
  SUSCOUNT spill_avail, chunk;
  SUCOMPLEX y, c, t, sum;
  SUCOMPLEX *bufdec = buffer;
  SUSCOUNT maxdec = max;

  if (!self->capturing)
    return 0;

  if (self->decim > 1) {
    result = 0;
    spill_avail = self->decim_spillover_size - self->decim_spillover_ptr;

    if (spill_avail > 0) {
      chunk = SU_MIN(maxdec, spill_avail);
      memcpy(
        bufdec,
        self->decim_spillover + self->decim_spillover_ptr,
        chunk * sizeof(SUCOMPLEX));
      self->decim_spillover_ptr += chunk;
      bufdec += chunk;
      maxdec -= chunk;
      result += chunk;

      if (self->decim_spillover_ptr == self->decim_spillover_size) {
        self->decim_spillover_ptr = 0;
        self->decim_spillover_size = 0;
      }
    }

    if (maxdec > 0) {
      self->curr_buf  = bufdec;
      self->curr_ptr  = 0;
      self->curr_size = maxdec;
      do {
        if ((got = (self->iface->read) (
          self->source,
          self->read_buf,
          SUSCAN_SOURCE_DEFAULT_BUFSIZ)) < 1)
          return got;

        suscan_source_feed_decimator(self, self->read_buf, got);
      } while(self->curr_ptr == 0);
      result += self->curr_ptr;
    }
  } else {
    result = (self->iface->read) (self->source, buffer, max);
  }

  if (result > 0)
    self->total_samples += result;

  if (result > 0) {
    if (self->soft_dc_correction) {
      c   = self->dc_c;
      sum = self->dc_offset;

      for (i = 0; i < result; ++i) {
        y = buffer[i] - c;
        t = sum + y;
        c = (t - sum) - y;
        sum = t;
      }

      if (self->soft_dc_train_samples > 0) {
        self->dc_c = c;
        self->dc_offset = sum;
        self->soft_dc_count += result;

        if (self->soft_dc_count > self->soft_dc_train_samples) {
          self->dc_offset /= self->soft_dc_count;
          self->soft_dc_correction = SU_FALSE;
          self->have_dc_offset = SU_TRUE;
        }
      } else {
        SU_SPLPF_FEED(self->dc_offset, sum / result, self->soft_dc_alpha);
      }
    }
    
    if (self->have_dc_offset) {
      for (i = 0; i < result; ++i)
        buffer[i] -= self->dc_offset;
    }
  }

  
  return result;
}

void 
suscan_source_get_time(suscan_source_t *self, struct timeval *tv)
{
  (self->iface->get_time) (self->source, tv);
}

SUSCOUNT 
suscan_source_get_consumed_samples(const suscan_source_t *self)
{
  return self->total_samples;
}

void
suscan_source_get_end_time(
  const suscan_source_t *self,
  struct timeval *tv)
{
  struct timeval start, elapsed = {0, 0};
  SUSDIFF max_size;
  SUFLOAT samp_rate = suscan_source_get_base_samp_rate(self);

  /* For this calculation WE WANT the base samp rate. The one without decimation. */
  suscan_source_get_start_time(self, &start);

  max_size = suscan_source_get_max_size(self) - 1;
  if (max_size >= 0) {
    elapsed.tv_sec  = max_size / samp_rate;
    elapsed.tv_usec = (1000000 
      * (max_size - elapsed.tv_sec * samp_rate))
      / samp_rate;
  }

  timeradd(&start, &elapsed, tv);
}

SUBOOL
suscan_source_seek(suscan_source_t *self, SUSCOUNT pos)
{
  if (self->iface->seek == NULL)
    return SU_FALSE;

  return (self->iface->seek) (self->source, pos);
}

SUSDIFF
suscan_source_get_max_size(const suscan_source_t *self)
{
  if (self->iface->max_size == NULL)
    return -1;

  return (self->iface->max_size) (self->source);
}

SUSCOUNT 
suscan_source_get_base_samp_rate(const suscan_source_t *self)
{
  return self->config->samp_rate;
}

SUBOOL
suscan_source_start_capture(suscan_source_t *source)
{
  if (source->capturing) {
    SU_WARNING("start_capture: called twice, already capturing!\n");
    return SU_TRUE;
  }

  if (!(source->iface->start) (source->source)) {
    SU_ERROR("Failed to start capture\n");
    return SU_FALSE;
  }

  source->capturing = SU_TRUE;

  return SU_TRUE;
}

SUBOOL
suscan_source_stop_capture(suscan_source_t *source)
{
  if (!source->capturing) {
    SU_WARNING("stop_capture: called twice, already capturing!\n");
    return SU_TRUE;
  }

  if (!(source->iface->cancel) (source->source)) {
    SU_ERROR("Failed to stop capture\n");
    return SU_FALSE;
  }

  source->capturing = SU_FALSE;

  return SU_TRUE;
}


SUBOOL
suscan_source_set_agc(suscan_source_t *self, SUBOOL set)
{
  if (!self->capturing)
    return SU_FALSE;

  if (self->iface->set_agc == NULL)
    return SU_FALSE;

  if (!(self->iface->set_agc) (self->source, set)) {
    SU_ERROR("Failed to change AGC state\n");
    return SU_FALSE;
  }

  return SU_TRUE;
}

SUBOOL
suscan_source_set_dc_remove(suscan_source_t *self, SUBOOL remove)
{
  if (!self->capturing)
    return SU_FALSE;

  if (self->use_soft_dc) {
    if (remove) {
      self->soft_dc_count = 0;
      self->dc_offset = 0;
      self->dc_c = 0;
    }

    self->soft_dc_correction = remove;
    self->have_dc_offset = remove && self->soft_dc_train_samples == 0;

    return SU_TRUE;
  } else {
    if (self->iface->set_dc_remove == NULL)
      return SU_FALSE;

    if (!(self->iface->set_dc_remove) (self->source, remove)) {
      SU_ERROR("Failed to set DC remove setting\n");
      return SU_FALSE;
    }
  }

  return SU_TRUE;
}

SUBOOL
suscan_source_set_gain(suscan_source_t *self, const char *name, SUFLOAT val)
{
  if (!self->capturing)
    return SU_FALSE;

  if (self->iface->set_gain == NULL)
    return SU_FALSE;

  if (!(self->iface->set_gain)(self->source, name, val)) {
    SU_ERROR("Failed to adjust source gain `%s'\n", name);
    return SU_FALSE;
  }

  return SU_TRUE;
}

SUBOOL
suscan_source_set_antenna(suscan_source_t *self, const char *name)
{
  if (!self->capturing)
    return SU_FALSE;

  if (self->iface->set_antenna == NULL)
    return SU_FALSE;

  if (!(self->iface->set_antenna)(self->source, name)) {
    SU_ERROR("Failed to switch to antenna `%s'\n", name);
    return SU_FALSE;
  }

  (void) suscan_source_config_set_antenna(self->config, name);

  return SU_TRUE;
}

SUBOOL
suscan_source_set_bandwidth(suscan_source_t *self, SUFLOAT bw)
{
  if (!self->capturing)
    return SU_FALSE;

  if (self->iface->set_bandwidth == NULL)
    return SU_FALSE;

  if (!(self->iface->set_bandwidth) (self->source, bw)) {
    SU_ERROR("Failed to set bandwidth\n");
    return SU_FALSE;
  }

  /* Update config */
  (void) suscan_source_config_set_bandwidth(self->config, bw);

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_source_set_decimator_freq(suscan_source_t *self, SUFREQ freq)
{
  SUFREQ fdiff, fmax;
  SUFLOAT frel;
  SUFLOAT native_rate, decimated_rate;
  SUBOOL ok = SU_FALSE;

  native_rate = self->info.source_samp_rate;
  
  if (self->decim > 1) {
    decimated_rate = native_rate / self->decim;
    fmax = (native_rate - decimated_rate) / 2;
    fdiff = (freq - self->config->freq);
    if (fdiff < -fmax || fdiff > fmax) {
      SU_ERROR("Decimator frequency out of bounds\n");
      goto done;
    }

    frel = SU_ABS2NORM_FREQ(native_rate, fdiff);
    su_specttuner_set_channel_freq(
      self->decimator,
      self->main_channel,
      SU_NORM2ANG_FREQ(frel));
  } else {
    return SU_TRUE;
  }

  ok = SU_TRUE;

done:
  return ok;
}

SUPRIVATE
SUBOOL suscan_source_set_freq2_internal(
  suscan_source_t *self,
  SUFREQ freq,
  SUFREQ lnb)
{
  SUBOOL ok = SU_FALSE;

  if (self->iface->set_frequency == NULL) {
    /* No native way to change the frequency: perform decimated spectrum navigation */
    SU_TRY(suscan_source_set_decimator_freq(self, freq));
  } else {
    if (!(self->iface->set_frequency)(self, freq - lnb)) {
      SU_ERROR("Failed to set frequency\n");
      goto done;
    }
  }

  ok = SU_TRUE;

done:
  return ok;
}

SUBOOL
suscan_source_set_freq(suscan_source_t *self, SUFREQ freq)
{
  if (!self->capturing)
    return SU_FALSE;

  if (!suscan_source_set_freq2_internal(self, freq, self->config->lnb_freq))
    return SU_FALSE;
  
  /* Update config */
  (void) suscan_source_config_set_freq(self->config, freq);

  return SU_TRUE;
}

SUBOOL
suscan_source_set_ppm(suscan_source_t *self, SUFLOAT ppm)
{
  if (!self->capturing)
    return SU_FALSE;

  if (self->iface->set_ppm == NULL)
    return SU_FALSE;

  if (!(self->iface->set_ppm) (self->source, ppm)) {
    SU_ERROR("Failed to set PPM\n");
    return SU_FALSE;
  }

  /* Update config */
  suscan_source_config_set_ppm(self->config, ppm);

  return SU_TRUE;
}

SUBOOL
suscan_source_set_lnb_freq(suscan_source_t *self, SUFREQ lnb)
{
  if (!self->capturing)
    return SU_FALSE;

  if (!suscan_source_set_freq2_internal(self, self->config->freq, lnb))
    return SU_FALSE;

  /* Update config */
  suscan_source_config_set_lnb_freq(self->config, lnb);

  return SU_TRUE;
}

SUBOOL
suscan_source_set_freq2(suscan_source_t *self, SUFREQ freq, SUFREQ lnb)
{
  if (!self->capturing)
    return SU_FALSE;

  if (!suscan_source_set_freq2_internal(self, freq, lnb))
    return SU_FALSE;

  /* Update config */
  suscan_source_config_set_freq(self->config, freq);
  suscan_source_config_set_lnb_freq(self->config, lnb);

  return SU_TRUE;
}

SUFREQ
suscan_source_get_freq(const suscan_source_t *self)
{
  SUFREQ fdiff;
  SUFLOAT native_rate;

  if (!self->capturing)
    return suscan_source_config_get_freq(self->config);
  
  if (self->iface->set_frequency == NULL) {
    native_rate = self->info.source_samp_rate;

    if (self->decim == 1)
      fdiff = 0;      
    else
      fdiff = SU_NORM2ABS_FREQ(
        native_rate,
        SU_ANG2NORM_FREQ(
          su_specttuner_channel_get_f0(self->main_channel)));

    return fdiff + suscan_source_config_get_freq(self->config);
  } else {
    return suscan_source_config_get_freq(self->config);
  }
}

SUPRIVATE SUBOOL
suscan_source_config_check(const suscan_source_config_t *config)
{
  if (config->average < 1) {
    SU_ERROR("Invalid averaging value. Should be at least 1 for no averaging\n");
    return SU_FALSE;
  }

  return SU_TRUE;
}

#define CHECK_MISSING(method, flag)               \
  if (self->iface->method == NULL)                \
    self->info.permissions &= ~JOIN(SUSCAN_ANALYZER_PERM_, flag)

SUPRIVATE void
suscan_source_adjust_permissions(suscan_source_t *self)
{
  CHECK_MISSING(set_frequency, SET_FREQ);
  CHECK_MISSING(set_gain,      SET_GAIN);
  CHECK_MISSING(set_antenna,   SET_ANTENNA);
  CHECK_MISSING(set_bandwidth, SET_BW);
  CHECK_MISSING(set_ppm,       SET_PPM);
  CHECK_MISSING(set_dc_remove, SET_DC_REMOVE);
  CHECK_MISSING(set_agc,       SET_AGC);
  CHECK_MISSING(seek,          SEEK);
  CHECK_MISSING(max_size,      SEEK);

  /* If source does not support DC remove, enable it by software */
  if (~self->info.permissions & SUSCAN_ANALYZER_PERM_SET_DC_REMOVE) {
    self->use_soft_dc           = SU_TRUE;
    self->soft_dc_train_samples = suscan_source_get_dc_samples(self);
    self->soft_dc_correction    = self->config->dc_remove;
    
    if (self->soft_dc_train_samples == 0) {
      self->soft_dc_alpha  = SU_SPLPF_ALPHA(SUSCAN_SOURCE_DC_AVERAGING_PERIOD);
      self->have_dc_offset = self->soft_dc_correction;
    }

    if (self->soft_dc_correction) {
      SU_INFO("Source does not support native DC correction, falling back to software correction\n");
      if (self->have_dc_offset) {
        SU_INFO("DC correction strategy: continuous\n");
      } else {
        SU_INFO(
          "DC correction strategy: one-shot (%" PRIu64 " samples)\n",
          self->soft_dc_train_samples);
      }
    }

    self->info.permissions |= SUSCAN_ANALYZER_PERM_SET_DC_REMOVE;
  }
}

#undef CHECK_MISSING

suscan_source_t *
suscan_source_new(suscan_source_config_t *config)
{
  suscan_source_t *new = NULL;

  SU_TRY_FAIL(suscan_source_config_check(config));
  SU_ALLOCATE_FAIL(new, suscan_source_t);
  SU_TRY_FAIL(new->config = suscan_source_config_clone(config));

  new->decim = 1;
  new->config = config;

  if (config->average > 1)
    SU_TRYCATCH(
        suscan_source_configure_decimation(new, config->average),
        goto fail);

  /* Search by index */
  new->iface = suscan_source_interface_lookup_by_index(config->type);

  if (new->iface == NULL) {
    SU_ERROR(
      "Unknown source type passed to config (index = %d)\n",
      config->type);
    goto fail;
  }

  /* Call the source constructor */
  new->source = new->iface->open(new, config, &new->info);
  if (new->source == NULL)
    goto fail;
  
  /* Done, adjust permissions */
  suscan_source_adjust_permissions(new);
  
  /* Some convenience flags */
  new->info.seekable = !!(new->info.permissions & SUSCAN_ANALYZER_PERM_SEEK);

  return new;

fail:
  if (new != NULL)
    suscan_source_destroy(new);

  return NULL;
}

SUBOOL
suscan_init_sources(void)
{
  const char *mcif;

#ifdef _WIN32
  WORD wVersionRequested;
  WSADATA wsaData;
  int err;

  wVersionRequested = MAKEWORD(2, 2);

  err = WSAStartup(wVersionRequested, &wsaData);
  if (err != 0) {
    SU_ERROR(
      "WSAStartup failed with error %d: network function will not work\n", 
      err);
  } else if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) {
    SU_ERROR(
      "Requested version of the Winsock API (2.2) is not available\n");
    WSACleanup();
  }
#endif /* _WIN32 */

  SU_TRYCATCH(suscan_source_init_source_types(), return SU_FALSE);

  /* TODO: Register analyzer interfaces? */
  SU_TRYCATCH(suscan_source_device_preinit(), return SU_FALSE);
  SU_TRYCATCH(suscan_source_register_null_device(), return SU_FALSE);
  SU_TRYCATCH(suscan_confdb_use("sources"), return SU_FALSE);
  SU_TRYCATCH(suscan_source_detect_devices(), return SU_FALSE);
  SU_TRYCATCH(suscan_load_sources(), return SU_FALSE);

  if ((mcif = getenv("SUSCAN_DISCOVERY_IF")) != NULL && strlen(mcif) > 0) {
    SU_INFO("Discovery mode started\n");
    if (!suscan_device_net_discovery_start(mcif)) {
      SU_ERROR("Failed to initialize remote device discovery.\n");
      SU_ERROR("SuRPC services will be disabled.\n");
    }
  }

  return SU_TRUE;
}
