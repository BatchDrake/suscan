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

#include "../src/suscan.h"

#include "source.h"
#include "compat.h"
#include "discovery.h"

#include <sigutils/taps.h>
#include <sigutils/specttuner.h>
#include <fcntl.h>
#include <unistd.h>
#include <sigutils/util/compat-time.h>
#include <sigutils/util/compat-mman.h>
#include <confdb.h>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  if defined(interface)
#    undef interface
#  endif /* interface */
#endif /* _WIN32 */


/****************************** Source API ***********************************/
void
suscan_source_destroy(suscan_source_t *self)
{
  if (self->src_priv != NULL)
    (self->iface->close) (self->src_priv);
  
  if (self->config != NULL)
    suscan_source_config_destroy(self->config);

  if (self->decimator != NULL)
    su_specttuner_destroy(self->decimator);

  if (self->decim_spillover != NULL)
    free(self->decim_spillover);

  if (self->read_buf != NULL)
    free(self->read_buf);

  if (self->throttle_mutex_init)
    pthread_mutex_destroy(&self->throttle_mutex);

  free(self);
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

SUINLINE SUSDIFF
suscan_source_read_samples(suscan_source_t *self, SUCOMPLEX *buffer, SUSCOUNT max)
{
  SUSDIFF got = 0;
  SUSCOUNT result = -1;
  SUSCOUNT spill_avail, chunk;
  SUCOMPLEX *bufdec = buffer;
  SUSCOUNT maxdec = max;

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
          self->src_priv,
          self->read_buf,
          SUSCAN_SOURCE_DEFAULT_BUFSIZ)) < 1)
          return got;

        if (self->dc_correction_enabled)
          su_dc_corrector_correct(&self->dc_corrector, self->read_buf, got);
        suscan_source_feed_decimator(self, self->read_buf, got);
      } while(self->curr_ptr == 0);
      result += self->curr_ptr;
    }
  } else {
    result = (self->iface->read) (self->src_priv, buffer, max);
    if (result > 0 && self->dc_correction_enabled)
      su_dc_corrector_correct(&self->dc_corrector, buffer, result);
  }

  return result;
}

SUINLINE void
suscan_source_history_write(
  suscan_source_t *self,
  const SUCOMPLEX *buffer,
  SUSCOUNT len)
{
  SUSCOUNT ptr = self->history_ptr;

  if (len > self->history_alloc) {
    buffer += len - self->history_alloc;
    len = self->history_alloc;
  }

  SUSCOUNT rem = len;
  SUSCOUNT avail = self->history_alloc - ptr;
  SUSCOUNT chunklen = MIN(len, avail);

  memcpy(self->history + ptr, buffer, chunklen * sizeof(SUCOMPLEX));

  rem    -= chunklen;
  buffer += chunklen;
  ptr    += chunklen;

  if (ptr == self->history_alloc) {
    ptr = 0;
  
    if (rem > 0) {
      memcpy(self->history, buffer, rem * sizeof(SUCOMPLEX));
      ptr += rem;
    }
  }

  if (self->history_size < self->history_alloc) {
    self->history_size += len;

    if (self->history_size > self->history_alloc)
      self->history_size = self->history_alloc;
  }

  self->history_ptr = ptr;
}

SUINLINE SUSDIFF
suscan_source_history_read(
  suscan_source_t *self,
  SUCOMPLEX *buffer,
  SUSCOUNT len)
{
  SUSCOUNT avail;

  if (self->history_size == 0)
    return 0;
  
  if (self->history_ptr >= self->history_size) {
    self->history_ptr %= self->history_size;
    suscan_source_mark_looped(self);
  }

  avail = self->history_size - self->history_ptr;

  if (len > avail)
    len = avail;

  memcpy(buffer, self->history + self->history_ptr, len * sizeof(SUCOMPLEX));

  self->history_ptr += len;

  return len;
}

SUSDIFF
suscan_source_read(suscan_source_t *self, SUCOMPLEX *buffer, SUSCOUNT max)
{
  SUSDIFF result = -1;

  if (!self->capturing)
    return 0;

  /* With non-real time sources, use throttle to control CPU usage */
  if (!suscan_source_is_real_time(self)) {
    SU_TRYZ(pthread_mutex_lock(&self->throttle_mutex));
    max = suscan_throttle_get_portion(&self->throttle, max);
    SU_TRYZ(pthread_mutex_unlock(&self->throttle_mutex));
  }
  
  if (self->history_enabled) {
    if (self->history_replay) {
      result = suscan_source_history_read(self, buffer, max);
    } else {
      result = suscan_source_read_samples(self, buffer, max);

      if (result > 0)
        suscan_source_history_write(self, buffer, result);
    }
  } else {
    /* No history, just regular read */
    result = suscan_source_read_samples(self, buffer, max);
  }

  if (result > 0)
    self->total_samples += result;

  if (!suscan_source_is_real_time(self)) {
    SU_TRYZ(pthread_mutex_lock(&self->throttle_mutex));
    suscan_throttle_advance(&self->throttle, result);
    SU_TRYZ(pthread_mutex_unlock(&self->throttle_mutex));
  }

done:
  return result;
}

SUBOOL
suscan_source_fill_buffer(
  suscan_source_t *self,
  suscan_sample_buffer_t *buffer,
  SUSCOUNT size,
  SUSDIFF *got)
{
  SUSCOUNT amount;
  SUSDIFF p = -1, read;
  SUCOMPLEX *data;
  SUBOOL ok = SU_FALSE;

  data = suscan_sample_buffer_data(buffer);

  p = 0;

  while (p < size) {
    amount = size - p;
    read = suscan_source_read(self, data + p, amount);

    /* Check for errors */
    if (read == 0)
      goto done;

    if (read < 0) {
      p = read;
      goto done;
    }

    p += read;
  }

  ok = SU_TRUE;

done:
  *got = p;

  return ok;
}

suscan_sample_buffer_t *
suscan_source_read_buffer(
  suscan_source_t *self,
  suscan_sample_buffer_pool_t *pool,
  SUSDIFF *got)
{
  suscan_sample_buffer_t *buffer = NULL;
  SUSCOUNT size;
  SUBOOL ok = SU_FALSE;

  /* Acquire buffer */
  SU_TRY(buffer = suscan_sample_buffer_pool_acquire(pool));
  size = suscan_sample_buffer_size(buffer);

  /* Populate it */
  SU_TRY(suscan_source_fill_buffer(self, buffer, size, got));

  ok = SU_TRUE;

done:
  if (!ok) {
    if (buffer != NULL) {
      suscan_sample_buffer_pool_give(pool, buffer);
      buffer = NULL;
    }
  }

  return buffer;
}

void 
suscan_source_get_time(suscan_source_t *self, struct timeval *tv)
{
  (self->iface->get_time) (self->src_priv, tv);
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

  return (self->iface->seek) (self->src_priv, pos * self->decim);
}

SUBOOL
suscan_source_override_throttle(suscan_source_t *self, SUSCOUNT val)
{
  SUBOOL mutex_acquired = SU_FALSE;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      pthread_mutex_lock(&self->throttle_mutex) != -1,
      goto done);
  mutex_acquired = SU_TRUE;

  suscan_throttle_init(&self->throttle, val);

  self->info.effective_samp_rate = val;

  ok = SU_TRUE;

done:

  if (mutex_acquired)
    pthread_mutex_unlock(&self->throttle_mutex);

  return ok;
}


SUSDIFF
suscan_source_get_max_size(const suscan_source_t *self)
{
  if (self->iface->max_size == NULL)
    return -1;

  return (self->iface->max_size) (self->src_priv);
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

  if (!(source->iface->start) (source->src_priv)) {
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

  if (!(source->iface->cancel) (source->src_priv)) {
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

  if (!(self->iface->set_agc) (self->src_priv, set)) {
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

  if (self->soft_dc) {
    if (remove)
      su_dc_corrector_reset(&self->dc_corrector);
    self->dc_correction_enabled = remove;
    return SU_TRUE;
  } else {
    if (self->iface->set_dc_remove == NULL)
      return SU_FALSE;

    if (!(self->iface->set_dc_remove) (self->src_priv, remove)) {
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

  if (!(self->iface->set_gain) (self->src_priv, name, val)) {
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

  if (!(self->iface->set_antenna) (self->src_priv, name)) {
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

  if (!(self->iface->set_bandwidth) (self->src_priv, bw)) {
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

  decimated_rate = self->info.source_samp_rate;
  native_rate    = self->info.source_samp_rate * self->decim;
  
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
    if (!(self->iface->set_frequency) (self->src_priv, freq - lnb)) {
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
  if (self->iface->set_frequency != NULL)
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

  if (!(self->iface->set_ppm) (self->src_priv, ppm)) {
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
  if (self->iface->set_frequency != NULL)
    (void) suscan_source_config_set_freq(self->config, freq);
  
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
  SUSCOUNT dc_samples;
  SUFREQ fdiff;
  SUFREQ f0;

  CHECK_MISSING(set_frequency, SET_FREQ);
  CHECK_MISSING(set_gain,      SET_GAIN);
  CHECK_MISSING(set_antenna,   SET_ANTENNA);
  CHECK_MISSING(set_bandwidth, SET_BW);
  CHECK_MISSING(set_ppm,       SET_PPM);
  CHECK_MISSING(set_dc_remove, SET_DC_REMOVE);
  CHECK_MISSING(set_agc,       SET_AGC);
  CHECK_MISSING(seek,          SEEK);
  CHECK_MISSING(max_size,      SEEK);

  /* If source is realtime, seeking and throttling are disabled */
  if (self->info.realtime) {
    self->info.permissions &= ~SUSCAN_ANALYZER_PERM_SEEK;
    self->info.permissions &= ~SUSCAN_ANALYZER_PERM_THROTTLE;
  }

  /* If source does not support DC remove, enable it by software */
  if (~self->info.permissions & SUSCAN_ANALYZER_PERM_SET_DC_REMOVE) {
    self->soft_dc               = SU_TRUE;
    self->dc_correction_enabled = self->config->dc_remove;

    dc_samples = suscan_source_get_dc_samples(self);
    if (dc_samples > 0)
      su_dc_corrector_init_with_training_period(&self->dc_corrector, dc_samples);
    else
      su_dc_corrector_init_with_alpha(
        &self->dc_corrector,
        SU_SPLPF_ALPHA(SUSCAN_SOURCE_DC_AVERAGING_PERIOD));
    
    if (self->soft_dc) {
      SU_INFO("Source does not support native DC correction, falling back to software correction\n");
      if (dc_samples == 0) {
        SU_INFO("DC correction strategy: continuous\n");
      } else {
        SU_INFO(
          "DC correction strategy: one-shot (%" PRIu64 " samples)\n",
          dc_samples);
      }
    }

    self->info.permissions |= SUSCAN_ANALYZER_PERM_SET_DC_REMOVE;
  }

  /* If source does not support frequency set, maybe it does under decimation */
  if (~self->info.permissions & SUSCAN_ANALYZER_PERM_SET_FREQ) {
    fdiff = suscan_source_get_base_samp_rate(self) 
          - suscan_source_get_samp_rate(self);
    f0   = suscan_source_config_get_freq(self->config);

    self->info.freq_min = f0 - fdiff / 2;
    self->info.freq_max = f0 + fdiff / 2;

    if (self->decim > 1)
      self->info.permissions |= SUSCAN_ANALYZER_PERM_SET_FREQ;
  }
}

#undef CHECK_MISSING

SUPRIVATE SUBOOL
suscan_source_populate_source_info(suscan_source_t *self)
{
  struct suscan_source_info *info = &self->info;
  const suscan_source_config_t *config = self->config;
  const char *ant = suscan_source_config_get_antenna(config);
  SUBOOL ok = SU_FALSE;

  /* Adjust by decimation */
  info->source_samp_rate    /= self->decim;
  info->effective_samp_rate /= self->decim;
  info->measured_samp_rate  /= self->decim;

  /* Some convenience flags */
  info->seekable = !!(info->permissions & SUSCAN_ANALYZER_PERM_SEEK);

  /* Get frequency */
  info->frequency  = suscan_source_get_freq(self);
  info->lnb        = suscan_source_config_get_lnb_freq(config);
  info->bandwidth  = suscan_source_config_get_bandwidth(config);
  info->dc_remove  = suscan_source_config_get_dc_remove(config);
  info->ppm        = suscan_source_config_get_ppm(config);
  info->iq_reverse = SU_FALSE;
  info->agc        = SU_FALSE;

  info->have_qth   = suscan_get_qth(&info->qth);

  suscan_source_get_time(self, &info->source_time);

  /* Seekable source: it has an start and end time */
  if (info->seekable) {
    suscan_source_get_start_time(self, &info->source_start);
    suscan_source_get_end_time(self, &info->source_end);
  }

  /* Set antenna */
  if (info->permissions & SUSCAN_ANALYZER_PERM_SET_ANTENNA)
    if (ant != NULL)
      SU_TRY(info->antenna = strdup(ant));

  ok = SU_TRUE;
  
done:
  return ok;
}

SUBOOL
suscan_source_set_history_enabled(suscan_source_t *self, SUBOOL enabled)
{
  if (enabled && self->history_alloc == 0) {
    SU_ERROR("Cannot enable history with no history allocation\n");
    return SU_FALSE;
  }


  if (self->history_enabled != enabled) {
    self->history_enabled = enabled;

    if (enabled)
      self->history_size = self->history_ptr = 0;
  }

  return SU_TRUE;
}

SUBOOL
suscan_source_set_history_alloc(suscan_source_t *self, size_t bytes)
{
  SUSCOUNT samples = __UNITS(bytes, sizeof(SUCOMPLEX));
  return suscan_source_set_history_length(self, samples);
}

SUBOOL
suscan_source_set_history_length(suscan_source_t *self, SUSCOUNT length)
{
  size_t new_alloc = length * sizeof(SUCOMPLEX);
  size_t old_alloc = self->history_alloc * sizeof(SUCOMPLEX);
  SUCOMPLEX *new_bytes;

  if (new_alloc == 0) {
    /* Clear previous history */
    suscan_source_clear_history(self);

    self->history_size    = self->history_ptr = 0;
    self->history_enabled = SU_FALSE;
    self->history_replay  = SU_FALSE;

    return SU_TRUE;
  }
  
  new_bytes = mmap(
    NULL,
    new_alloc,
    PROT_READ | PROT_WRITE,
    MAP_PRIVATE | MAP_ANONYMOUS,
    0, 0);

  if (new_bytes == (SUCOMPLEX *) -1) {
    SU_ERROR(
      "Cannot mmap %lld bytes of memory: %s\n", new_alloc, strerror(errno));
    return SU_FALSE;
  }

  /* 
   * Now we have to copy the previous history to the new one. We need to
   * transfer everything from (ptr - size) to offset 0 in the new buffer.
   */
  if (old_alloc > 0) {
    SUSCOUNT p, copy_size;

    /* Partially full buffer, guess where to start */
    copy_size = self->history_size;
    p = copy_size < old_alloc ? 0 : self->history_ptr;
    
    /* Adjust start according to new alloc */
    if (copy_size > new_alloc) {
      p += copy_size - new_alloc;
      copy_size = new_alloc;
    }

    /* If p + copy_size surpasses the buffer size, divide in two */
    if (p + copy_size > old_alloc) {
      SUSCOUNT size1 = old_alloc - p;
      SUSCOUNT size2 = copy_size - size1;

      memcpy(new_bytes, self->history + p,     size1 * sizeof(SUCOMPLEX));
      memcpy(new_bytes + size1, self->history, size2 * sizeof(SUCOMPLEX));
    } else {
      memcpy(new_bytes, self->history + p, copy_size * sizeof(SUCOMPLEX));
    }

    self->history_size = self->history_ptr = copy_size;
    
    if (self->history_ptr == self->history_size)
      self->history_ptr = 0;
  } else {
    self->history_ptr = self->history_size = 0;
  }

  suscan_source_clear_history(self);

  self->history = new_bytes;
  self->history_alloc = new_alloc;
  
  return SU_TRUE;
}

SUSCOUNT
suscan_source_get_history_length(const suscan_source_t *self)
{
  return self->history_alloc;
}

SUBOOL
suscan_source_set_replay_enabled(suscan_source_t *self, SUBOOL enabled)
{
  if (self->history_alloc == 0 && enabled) {
    SU_ERROR("Cannot enable replay: no history allocated\n");
    return SU_FALSE;
  }

  if (self->history_replay != enabled) {
    /* When replay is enabled, we need to change the source start */
    if (enabled) {
      struct timeval diff;
      SUDOUBLE fs = self->info.source_samp_rate;
      SUSCOUNT us = 1e6 * self->history_size / fs;

      diff.tv_sec  = us / 1000000;
      diff.tv_usec = us % 1000000;

      suscan_source_get_time(self, &self->info.source_end);
      timersub(&self->info.source_end, &diff, &self->info.source_start);
    }

    self->history_replay = enabled;
    self->info.replay    = enabled;
  }
  
  return SU_TRUE;
}

void
suscan_source_clear_history(suscan_source_t *self)
{
  if (self->history_alloc > 0) {
    munmap(self->history, self->history_alloc);
    self->history_alloc = 0;
    self->history = NULL;
  }
}

suscan_source_t *
suscan_source_new(suscan_source_config_t *config)
{
  suscan_source_t *new = NULL;

  SU_TRY_FAIL(suscan_source_config_check(config));
  SU_ALLOCATE_FAIL(new, suscan_source_t);
  
  SU_TRY_FAIL(new->config = suscan_source_config_clone(config));

  new->decim = 1;

  if (config->average > 1)
    SU_TRYCATCH(
        suscan_source_configure_decimation(new, config->average),
        goto fail);

  /* Search by index */
  new->iface = suscan_source_interface_lookup_by_name(config->type);

  if (new->iface == NULL) {
    SU_ERROR("Unknown source type `%s' passed to config\n", config->type);
    goto fail;
  }

  /* Call the source constructor */
  new->src_priv = (new->iface->open) (new, new->config, &new->info);
  if (new->src_priv == NULL)
    goto fail;
  
  /* Done, adjust permissions */
  suscan_source_adjust_permissions(new);
  suscan_source_populate_source_info(new);

  /* Initialize throttle (if applicable) */
  if (!suscan_source_is_real_time(new)) {
    /* Create throttle mutex */
      (void) pthread_mutex_init(&new->throttle_mutex, NULL); /* Always succeeds */
      new->throttle_mutex_init = SU_TRUE;

    suscan_throttle_init(&new->throttle, new->info.effective_samp_rate);
  }

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
