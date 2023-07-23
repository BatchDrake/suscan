/*

  Copyright (C) 2020 Gonzalo José Carracedo Carballal

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

#define SU_LOG_DOMAIN "power-inspector"

#include <sigutils/sigutils.h>
#include <sigutils/agc.h>
#include <sigutils/pll.h>
#include <sigutils/sampling.h>
#include <sigutils/iir.h>
#include <sigutils/clock.h>

#include <analyzer/version.h>

#include "inspector/interface.h"
#include "inspector/params.h"
#include "inspector/inspector.h"

#include <string.h>

#ifdef SU_USE_VOLK
#  include <volk/volk.h>
#  ifdef INCLUDED_volk_32fc_x2_conjugate_dot_prod_32fc_a_H
#    define HAVE_VOLK_ACCELERATION
#  endif /* INCLUDED_volk_32fc_x2_conjugate_dot_prod_32fc_a_H */
#endif /* SU_USE_VOLK */

/*
 * This comes from the fact that, when performing power measurements in the
 * frequency domain, the spectrum has been filtered by a raised cosine
 * window (also known as Hann window). This window is 1 in the center and
 * 0 in the edges. If you estimate its power, you will see it is 0.375. Or,
 * in faction form, 3. / 8. We compensate this effect by multiplying the result
 * by this factor in the end, so that time and frequency power estimations
 * are the same.
 */
#define SU_POWER_INSPECTOR_FFT_WINDOW_INV_GAIN (8. / 3.)

/*
 * The power inspector works by computing the energy of the received samples,
 * and dividing them by the number of samples. The Parseval theorem states
 * that the energy computed in the time domain must equal the energy computed
 * in the frequency domain. We can exploit this theorem to speed up the 
 * calculation of the RMS for very long integration times. We identify
 * two modes of operation:
 * 
 *  TIME DOMAIN MODE (when integrate_samples < fft_size):
 *     We are running below the time resolution of the FFT. We compute the
 *     inverse Fourier transform of the samples and perform the energy
 *     calculation in the time domain.
 * 
 * FREQUENCY DOMAIN MODE (when integrate_samples >= fft_size):
 *     We are running well above the time resolution of the FFT. Additionally,
 *     FFTs are overlapped. If we call K = integrate_samples / fft_size, and
 *     alpha = K - floor(K), we operate as follows:
 * 
 *     1. Keep a variable named E_p (init: 0) and beta (init: 0).
 *     2. Calculate K as integrate_samples / fft_size                  <-- Number of full FFTs to take into account
 *     3. Calculate Kr = K - beta                                      <-- Fraction already computed
 *     3. Calculate alpha = Kr - floor(Kr)                             <-- Fraction of the partial (next) FFT
 *     4. Calculate the total energy of floor(Kr) fft buffers (E)      <-- Whole FFTs, may be 0
 *     5. Calculate the total energy of the next fft buffer (E_n)      <-- Partial (next) FFT
 *     6. Compute E_t = beta * E_p + E + alpha * E_n                   <-- Whole part + partial next + partial previous
 *     7. Return P = E_t / K                                           <-- Divide by the total number of FFTs
 *     8. Save E_p = E_n                                               <-- Now this is the previous
 *     9. Save beta = 1 - alpha
 */
struct suscan_power_inspector_params {
  SUSCOUNT integrate_samples;
};

struct suscan_power_inspector {
  struct suscan_inspector_sampling_info samp_info;
  struct suscan_power_inspector_params req_params;
  struct suscan_power_inspector_params cur_params;

  suscan_inspector_t *insp;

  SUBOOL   frequency_mode;
  SUBOOL   stable;

  /* For time and frequency domains */
  SUFLOAT  pwr_kahan_acc;
  SUFLOAT  pwr_kahan_c;
  SUSCOUNT pwr_count;

  /* For frequency domain */
  SUFLOAT  K;
  SUFLOAT  Kr;
  SUSCOUNT Kr_total_samples; /* floor(Kr) * fft_size */
  
  SUFLOAT  alpha, beta;
  SUFLOAT  E_p;
  SUFLOAT  E;
  SUBOOL   last_piece;
};

SUPRIVATE void
suscan_power_inspector_params_initialize(
    struct suscan_power_inspector_params *params,
    const struct suscan_inspector_sampling_info *sinfo)
{
  memset(params, 0, sizeof(struct suscan_power_inspector_params));
}

SUPRIVATE void
suscan_power_inspector_destroy(struct suscan_power_inspector *self)
{
  free(self);
}

SUPRIVATE struct suscan_power_inspector *
suscan_power_inspector_new(const struct suscan_inspector_sampling_info *sinfo)
{
  struct suscan_power_inspector *new = NULL;

  SU_ALLOCATE_FAIL(new, struct suscan_power_inspector);

  new->samp_info = *sinfo;
  
  suscan_power_inspector_params_initialize(&new->cur_params, sinfo);

  return new;

fail:
  if (new != NULL)
    suscan_power_inspector_destroy(new);

  return NULL;
}

/************************** API implementation *******************************/
void *
suscan_power_inspector_open(const struct suscan_inspector_sampling_info *s)
{
  return suscan_power_inspector_new(s);
}

SUBOOL
suscan_power_inspector_get_config(void *private, suscan_config_t *config)
{
  struct suscan_power_inspector *self = (struct suscan_power_inspector *) private;

  SU_TRYCATCH(
    suscan_config_set_integer(
      config,
      "power.integrate-samples",
      self->cur_params.integrate_samples),
    return SU_FALSE);
  
  return SU_TRUE;
}

SUBOOL
suscan_power_inspector_parse_config(void *private, const suscan_config_t *config)
{
  struct suscan_field_value *value;
  struct suscan_power_inspector *self = (struct suscan_power_inspector *) private;

  SU_TRYCATCH(
      value = suscan_config_get_value(
          config,
          "power.integrate-samples"),
      return SU_FALSE);

  SU_TRYCATCH(value->field->type == SUSCAN_FIELD_TYPE_INTEGER, return SU_FALSE);

  self->req_params.integrate_samples = value->as_int;

  return SU_TRUE;
}

/* Called inside inspector mutex */
void
suscan_power_inspector_commit_config(void *private)
{
  struct suscan_power_inspector *self = 
    (struct suscan_power_inspector *) private;

  self->cur_params = self->req_params;
  self->pwr_count = 0;
  self->pwr_kahan_acc = 0;
  self->pwr_kahan_c = 0;

  self->frequency_mode = 
    self->cur_params.integrate_samples >= self->samp_info.fft_size;

  if (self->frequency_mode) {
    self->E_p        = 0;
    self->beta       = 0;
    self->last_piece = SU_FALSE;
    self->K          = 
      (SUFLOAT) self->cur_params.integrate_samples / (SUFLOAT) self->samp_info.fft_size;
  }

  if (self->insp != NULL)
    suscan_inspector_set_domain(self->insp, self->frequency_mode);
}

#ifdef HAVE_VOLK_ACCELERATION
SUINLINE SUSDIFF
suscan_power_inspector_feed_time_domain_volk(
  struct suscan_power_inspector *self,
  const SUCOMPLEX *x,
    SUSCOUNT count)
{
  suscan_inspector_t *insp = self->insp;
  SUSCOUNT i, max = self->cur_params.integrate_samples, ptr;
  SUFLOAT acc, t, y, c;
  lv_32fc_t power;
  SUSCOUNT avail;

  acc = self->pwr_kahan_acc;
  c   = self->pwr_kahan_c;
  ptr = self->pwr_count;

  i = 0;

  while (i < count && suscan_inspector_sampler_buf_avail(insp) > 0) {
    avail = SU_MIN(count - i, max - ptr);
    volk_32fc_x2_conjugate_dot_prod_32fc(&power, x + i, x + i, avail);
 
    y = SU_C_REAL(power) - c;
    t = acc + y;

    c = (t - acc) - y;
    acc = t;

    ptr += avail;
    
    if (ptr >= max) {
      suscan_inspector_push_sample(insp, acc / max);
      ptr = 0;
      acc = 0;
      c   = 0;
    }

    i += avail;
  }

  self->pwr_kahan_acc = acc;
  self->pwr_kahan_c   = c;
  self->pwr_count     = ptr;

  return i;
}

SUINLINE SUSDIFF
suscan_power_inspector_feed_freq_domain_volk(
  struct suscan_power_inspector *self,
  const SUCOMPLEX *x,
    SUSCOUNT count)
{
  suscan_inspector_t *insp = self->insp;
  SUSCOUNT i, max, max_n, ptr;
  SUFLOAT acc, t, y, c;
  SUFLOAT E, E_t, E_n, E_p;
  SUSCOUNT avail = 0;
  lv_32fc_t power;

  acc   = self->pwr_kahan_acc;
  c     = self->pwr_kahan_c;
  ptr   = self->pwr_count;
  max   = self->Kr_total_samples;
  E     = self->E;
  E_p   = self->E_p;
  max_n = max + self->samp_info.fft_size;

  i = 0;

  if (count != self->samp_info.fft_size)
    return count;
  
  while (i < count && suscan_inspector_sampler_buf_avail(insp) > 0) {
    if (ptr == 0) {
      self->Kr = self->K - self->beta;
      self->alpha = self->Kr - SU_FLOOR(self->Kr);
      max = SU_FLOOR(self->Kr) * self->samp_info.fft_size;
      max_n = max + self->samp_info.fft_size;
      E = 0;
    }
    
    if (ptr < max) {
      avail = SU_MIN(count - i, max - ptr);
    } else if (ptr < max_n) {
      avail = SU_MIN(count - i, max_n - ptr);
    } else {
      SU_ERROR("Error condition inside power meter (ptr >= max_n?)\n");
      return 0;
    }

    volk_32fc_x2_conjugate_dot_prod_32fc(&power, x + i, x + i, avail);

    y = SU_C_REAL(power) - c;
    t = acc + y;

    c = (t - acc) - y;
    acc = t;

    ptr += avail;

    if (ptr == max) {
      /* First floor(Kr) samples: calculate E. */
      E = acc;

      acc = 0;
      c   = 0;
    } else if (ptr == max_n) {
      /* Got next. */
      E_n = acc;
      E_t = self->beta * E_p + E + self->alpha * E_n;

      suscan_inspector_push_sample(
        insp,
        E_t / self->K * SU_POWER_INSPECTOR_FFT_WINDOW_INV_GAIN);
      E_p = E_n;
      self->beta = 1 - self->alpha;

      ptr = 0;
      acc = 0;
      c   = 0;
    }

    i += avail;
  }

  self->Kr_total_samples = max;
  self->pwr_kahan_acc    = acc;
  self->pwr_kahan_c      = c;
  self->pwr_count        = ptr;
  self->E                = E;
  self->E_p              = E_p;

  return i;
}
#else
SUINLINE SUSDIFF
suscan_power_inspector_feed_time_domain(
  struct suscan_power_inspector *self,
  const SUCOMPLEX *x,
    SUSCOUNT count)
{
  suscan_inspector_t *insp = self->insp;
  SUSCOUNT i, max = self->cur_params.integrate_samples, ptr;
  SUFLOAT acc, t, y, c, power;
  
  acc = self->pwr_kahan_acc;
  c   = self->pwr_kahan_c;
  ptr = self->pwr_count;


  for (i = 0; i < count && suscan_inspector_sampler_buf_avail(insp) > 0; ++i) {
    power = SU_C_REAL(x[i] * SU_C_CONJ(x[i]));
    y = power - c;
    t = acc + y;

    c = (t - acc) - y;
    acc = t;

    ++ptr;
    
    if (ptr >= max) {
      suscan_inspector_push_sample(insp, acc / max);
      ptr = 0;
      acc = 0;
      c   = 0;
    }
  }

  self->pwr_kahan_acc = acc;
  self->pwr_kahan_c   = c;
  self->pwr_count     = ptr;

  return i;
}

SUINLINE SUSDIFF
suscan_power_inspector_feed_freq_domain(
  struct suscan_power_inspector *self,
  const SUCOMPLEX *x,
    SUSCOUNT count)
{
  suscan_inspector_t *insp = self->insp;
  SUSCOUNT i, max, max_n, ptr;
  SUFLOAT acc, t, y, c, power;
  SUFLOAT E, E_t, E_n, E_p;

  acc   = self->pwr_kahan_acc;
  c     = self->pwr_kahan_c;
  ptr   = self->pwr_count;
  max   = self->Kr_total_samples;
  E     = self->E;
  E_p   = self->E_p;
  max_n = max + self->samp_info.fft_size;

  for (i = 0; i < count && suscan_inspector_sampler_buf_avail(insp) > 0; ++i) {
    /* First sample: initialize Kr and alpha */
    if (ptr == 0) {
      self->Kr = self->K - self->beta;
      self->alpha = self->Kr - SU_FLOOR(self->Kr);
      max = SU_FLOOR(self->Kr) * self->samp_info.fft_size;
      max_n = max + self->samp_info.fft_size;
      E = 0;
    }

    power = SU_C_REAL(x[i] * SU_C_CONJ(x[i]));
    y = power - c;
    t = acc + y;

    c = (t - acc) - y;
    acc = t;

    ++ptr;
    
    if (ptr == max) {
      /* First floor(Kr) samples: calculate E. */
      E = acc;
      acc = 0;
      c   = 0;
    } else if (ptr == max_n) {
      /* Got next. */
      E_n = acc;
      E_t = self->beta * E_p + E + self->alpha * E_n;
      suscan_inspector_push_sample(
        insp,
        E_t / self->K * SU_POWER_INSPECTOR_FFT_WINDOW_INV_GAIN);
      E_p = E_n;
      self->beta = 1 - self->alpha;

      ptr = 0;
      acc = 0;
      c   = 0;
    }
  }

  self->Kr_total_samples = max;
  self->pwr_kahan_acc    = acc;
  self->pwr_kahan_c      = c;
  self->pwr_count        = ptr;
  self->E                = E;
  self->E_p              = E_p;

  return i;
}
#endif

SUSDIFF
suscan_power_inspector_feed(
    void *private,
    suscan_inspector_t *insp,
    const SUCOMPLEX *x,
    SUSCOUNT count)
{
  struct suscan_power_inspector *self = 
    (struct suscan_power_inspector *) private;

  /* Some sanity checks */
  if (insp != NULL)
    self->insp = insp;
  
  /* Make sure we are in the right domain to being with */
  if (suscan_inspector_is_freq_domain(insp) != self->frequency_mode) {
    suscan_inspector_set_domain(insp, self->frequency_mode);
    return count;
  }

  if (self->cur_params.integrate_samples == 0)
    return count;
  
  if (self->stable) {
#ifdef HAVE_VOLK_ACCELERATION
    if (self->frequency_mode)
      return suscan_power_inspector_feed_freq_domain_volk(self, x, count);
    else
      return suscan_power_inspector_feed_time_domain_volk(self, x, count);
#else
    if (self->frequency_mode)
      return suscan_power_inspector_feed_freq_domain(self, x, count);
    else
      return suscan_power_inspector_feed_time_domain(self, x, count);
#endif
  } else {
    self->stable = SU_TRUE;
    return count;
  }
}

void
suscan_power_inspector_close(void *private)
{
  struct suscan_power_inspector *self = (struct suscan_power_inspector *) private;

  suscan_power_inspector_destroy(self);
}

SUPRIVATE struct suscan_inspector_interface iface = {
    .name = "power",
    .desc = "Power channel inspector",
    .frequency_domain = SU_TRUE,
    .open = suscan_power_inspector_open,
    .get_config = suscan_power_inspector_get_config,
    .parse_config = suscan_power_inspector_parse_config,
    .commit_config = suscan_power_inspector_commit_config,
    .feed = suscan_power_inspector_feed,
    .close = suscan_power_inspector_close
};

SUBOOL
suscan_power_inspector_register(void)
{
  suscan_config_desc_t *desc = NULL;

  SU_TRY_FAIL(
      desc = suscan_config_desc_new_ex(
          "power-params-desc-" SUSCAN_VERSION_STRING));

  SU_TRY_FAIL(
    suscan_config_desc_add_field(
      desc,
      SUSCAN_FIELD_TYPE_INTEGER,
      SU_FALSE,
      "power.integrate-samples",
      "Number of samples to integrate"));

  iface.cfgdesc = desc;
  desc = NULL;
  SU_TRY_FAIL(suscan_config_desc_register(iface.cfgdesc));

  (void) suscan_inspector_interface_add_spectsrc(&iface, "psd");
  
  /* Register inspector interface */
  SU_TRY_FAIL(suscan_inspector_interface_register(&iface));

  return SU_TRUE;

fail:
  if (desc != NULL)
    suscan_config_desc_destroy(desc);

  return SU_FALSE;
}
