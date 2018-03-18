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

#define SU_LOG_DOMAIN "hackrf"

#include <config.h>
#include <string.h>
#include "source.h"

#ifdef HAVE_HACKRF

#include <sources/hack_rf.h>

SUPRIVATE int
hackRF_rx_callback(hackrf_transfer* transfer)
{
  SUCOMPLEX *start;
  SUSDIFF size;
  SUFLOAT val;
  struct hackRF_state *state = (struct hackRF_state *) transfer->rx_ctx;
  int i;

  pthread_mutex_lock(&state->lock);

  for (i = 0; i < transfer->valid_length; ++i) {
    val = (transfer->buffer[i] ^ 0x80) / 128.;
    if (!state->toggle_iq) {
      /* Build real part */
      state->samp = val;
    } else {
      /* Build imaginary part and append */
      state->samp += I * val;
      su_stream_write(&state->stream, &state->samp, 1);
    }

    state->toggle_iq = !state->toggle_iq;
  }

  pthread_cond_signal(&state->cond);
  pthread_mutex_unlock(&state->lock);

  return 0;
}

SUPRIVATE void
hackRF_state_destroy(struct hackRF_state *state)
{
  int result;

  if (state->rx_started) {
    result = hackrf_stop_rx(state->dev);
    if (result != HACKRF_SUCCESS) {
      SU_ERROR("Failed to stop HackRF RX, memory leak ahead\n");
      return;
    }
  }

  if (state->dev != NULL)
    hackrf_close(state->dev);

  pthread_mutex_destroy(&state->lock);
  pthread_cond_destroy(&state->cond);

  su_stream_finalize(&state->stream);

  free(state);
}

SUPRIVATE struct hackRF_state *
hackRF_state_new(const struct hackRF_params *params)
{
  struct hackRF_state *new = NULL;
  int result;

  SU_TRYCATCH(new = calloc(1, sizeof (struct hackRF_state)), goto fail);

  new->params = *params;

  if (new->params.bufsiz == 0)
    new->params.bufsiz = HACKRF_STREAM_SIZE;

  SU_TRYCATCH(pthread_mutex_init(&new->lock, NULL) != -1, goto fail);

  SU_TRYCATCH(pthread_cond_init(&new->cond, NULL) != -1, goto fail);

  SU_TRYCATCH (su_stream_init(&new->stream, new->params.bufsiz), goto fail);

  if (params->serial == NULL || strlen(params->serial) == 0)
    result = hackrf_open(&new->dev);
  else
    result = hackrf_open_by_serial(params->serial, &new->dev);

  if (result != HACKRF_SUCCESS) {
    SU_ERROR(
        "Failed to open HackRF device: %s (%d)\n",
        hackrf_error_name(result),
        result);
    goto fail;
  }

  result = hackrf_set_sample_rate(new->dev, params->samp_rate);
  if (result != HACKRF_SUCCESS) {
    SU_ERROR(
        "Failed to set sample rate of HackRF device: %s (%d)\n",
        hackrf_error_name(result),
        result);
    goto fail;
  }

  new->samp_rate = params->samp_rate;

  result = hackrf_set_freq(new->dev, params->fc);
  if (result != HACKRF_SUCCESS) {
    SU_ERROR(
        "Failed to set center frequency of HackRF device: %s (%d)\n",
        hackrf_error_name(result),
        result);
    goto fail;
  }

  new->fc = params->fc;

  result = hackrf_set_vga_gain(new->dev, params->vga_gain);
  if (result != HACKRF_SUCCESS) {
    SU_ERROR(
        "Failed to set VGA (BB) gain HackRF device: %s (%d)\n",
        hackrf_error_name(result),
        result);
    goto fail;
  }

  result = hackrf_set_lna_gain(new->dev, params->vga_gain);
  if (result != HACKRF_SUCCESS) {
    SU_ERROR(
        "Failed to set LNA (IF) gain HackRF device: %s (%d)\n",
        hackrf_error_name(result),
        result);
    goto fail;
  }

  result = hackrf_set_amp_enable(new->dev, params->amp_enable);
  if (result != HACKRF_SUCCESS) {
    SU_ERROR(
        "Failed to set amplifier configuration HackRF device: %s (%d)\n",
        hackrf_error_name(result),
        result);
    goto fail;
  }

  result = hackrf_set_antenna_enable(new->dev, params->bias);
  if (result != HACKRF_SUCCESS) {
    SU_ERROR(
        "Failed to set bias tee configuration HackRF device: %s (%d)\n",
        hackrf_error_name(result),
        result);
    goto fail;
  }

#if 0
  result = hackrf_set_hw_sync_mode(new->dev, SU_TRUE);
  if (result != HACKRF_SUCCESS) {
    SU_ERROR(
        "Failed to set hardware sync mode of HackRF device: %s (%d)\n",
        hackrf_error_name(result),
        result);
    goto fail;
  }
#endif

  return new;

fail:
  if (new != NULL)
    hackRF_state_destroy(new);

  return NULL;
}

SUPRIVATE void
su_block_hackRF_dtor(void *private)
{
  struct hackRF_state *state = (struct hackRF_state *) private;

  hackRF_state_destroy(state);
}

SUPRIVATE SUBOOL
su_block_hackRF_ctor(struct sigutils_block *block, void **private, va_list ap)
{
  struct hackRF_state *state = NULL;
  struct hackRF_params *params;

  params = va_arg(ap, struct hackRF_params *);

  if ((state = hackRF_state_new(params)) == NULL) {
    SU_ERROR("Create hackRF state failed\n");
    goto fail;
  }

  /* Needed by modems, etc */
  if (!su_block_set_property_ref(
      block,
      SU_PROPERTY_TYPE_INTEGER,
      "samp_rate",
      &state->samp_rate)) {
    SU_ERROR("Expose samp_rate failed\n");
    goto fail;
  }

  if (!su_block_set_property_ref(
      block,
      SU_PROPERTY_TYPE_INTEGER,
      "fc",
      &state->fc)) {
    SU_ERROR("Expose fc failed\n");
    goto fail;
  }

  *private = state;

  return SU_TRUE;

fail:
  if (state != NULL)
    hackRF_state_destroy(state);

  return SU_FALSE;
}

#ifdef HACKRF_SAVE_SAMPLES
FILE *fp;
#endif /* HACKRF_SAVE_SAMPLES */

SUPRIVATE SUSDIFF
su_block_hackRF_acquire(
    void *priv,
    su_stream_t *out,
    unsigned int port_id,
    su_block_port_t *in)
{
  struct hackRF_state *state = (struct hackRF_state *) priv;
  SUSDIFF size;
  SUSDIFF got;
  SUCOMPLEX *start;
  unsigned int i;
  int result;

#ifdef HACKRF_SAVE_SAMPLES
  complex float iq;

  if (fp == NULL)
    fp = fopen("output.raw", "wb");
#endif /* HACKRF_SAVE_SAMPLES */

  /* Get the number of complex samples to acquire */
  size = su_stream_get_contiguous(
      out,
      &start,
      SU_MIN(state->params.bufsiz, out->size));

  /* Ensure that RX has been started */
  if (!state->rx_started) {
    result = hackrf_start_rx(state->dev, hackRF_rx_callback, state);

    if (result != HACKRF_SUCCESS) {
      SU_ERROR(
          "Failed to start RX on HackRF: %s (%d)\n",
          hackrf_error_name(result),
          result);
      return -1;
    }

    state->rx_started = SU_TRUE;
  }

  /* Acquire samples */
  pthread_mutex_lock(&state->lock);
  do {
    got = su_stream_read(&state->stream, su_stream_tell(out), start, size);

    if (got == 0) {
      pthread_cond_wait(&state->cond, &state->lock);
    } else if (got == -1) {
      SU_WARNING(
          "HackRF is delivering samples way too fast: samples lost (%lld)\n",
          state->stream.pos - out->pos);
      SU_WARNING(
          "Try incrementing buffer size\n");
      out->pos = state->stream.pos;
    }
  } while (got < 1);
  pthread_mutex_unlock(&state->lock);

  /* Increment position */
  if (su_stream_advance_contiguous(out, got) != got) {
    SU_ERROR("Unexpected size after su_stream_advance_contiguous\n");
    return -1;
  }

  return got;
}

SUPRIVATE struct sigutils_block_class su_block_class_HACKRF = {
    "hackRF", /* name */
    0,         /* in_size */
    1,         /* out_size */
    su_block_hackRF_ctor,     /* constructor */
    su_block_hackRF_dtor,     /* destructor */
    su_block_hackRF_acquire, /* acquire */
};

SUPRIVATE su_block_t *
suscan_hackRF_source_ctor(const struct suscan_source_config *config)
{
  struct hackRF_params params = sigutils_hackRF_params_INITIALIZER;
  const struct suscan_field_value *value;

  if ((value = suscan_source_config_get_value(config, "serial")) == NULL)
    return NULL;
  if (value->set)
    params.serial = value->as_string;

  if ((value = suscan_source_config_get_value(config, "fs")) == NULL)
    return NULL;
  if (value->set)
    params.samp_rate = value->as_int;

  if ((value = suscan_source_config_get_value(config, "fc")) == NULL)
    return NULL;
  if (value->set)
    params.fc = value->as_int;

  if ((value = suscan_source_config_get_value(config, "vga_gain")) == NULL)
    return NULL;
  if (value->set)
    params.vga_gain = value->as_int;

  if ((value = suscan_source_config_get_value(config, "lna_gain")) == NULL)
    return NULL;
  if (value->set)
    params.lna_gain = value->as_int;

  if ((value = suscan_source_config_get_value(config, "amp")) == NULL)
    return NULL;
  if (value->set)
    params.amp_enable = value->as_bool;

  if ((value = suscan_source_config_get_value(config, "bias")) == NULL)
    return NULL;
  if (value->set)
    params.bias = value->as_bool;

  if ((value = suscan_source_config_get_value(config, "bufsiz")) == NULL)
    return NULL;
  if (value->set)
    params.bufsiz = value->as_int;

  return su_block_new("hackRF", &params);
}

SUBOOL
suscan_hackRF_source_init(void)
{
  struct suscan_source *source = NULL;
  int result;

  result = hackrf_init();
  if (result != HACKRF_SUCCESS) {
    SU_WARNING(
        "hackrf_init() failed: %s (%d), HackRF source will not be available",
        hackrf_error_name(result),
        result);
    return SU_TRUE;
  }

  if (!su_block_class_register(&su_block_class_HACKRF))
    return SU_FALSE;

  if ((source = suscan_source_register(
      "hackRF",
      "Great Scott Gadgets' HackRF",
      suscan_hackRF_source_ctor)) == NULL)
    return SU_FALSE;

  source->real_time = SU_TRUE;

  if (!suscan_source_add_field(
      source,
      SUSCAN_FIELD_TYPE_STRING,
      SU_TRUE,
      "serial",
      "Serial number"))
    return SU_FALSE;

  if (!suscan_source_add_field(
      source,
      SUSCAN_FIELD_TYPE_INTEGER,
      SU_TRUE,
      "fs",
      "Sampling frequency"))
    return SU_FALSE;

  if (!suscan_source_add_field(
      source,
      SUSCAN_FIELD_TYPE_INTEGER,
      SU_TRUE,
      "fc",
      "Center frequency"))
    return SU_FALSE;

  if (!suscan_source_add_field(
      source,
      SUSCAN_FIELD_TYPE_INTEGER,
      SU_TRUE,
      "vga_gain",
      "VGA gain"))
    return SU_FALSE;

  if (!suscan_source_add_field(
      source,
      SUSCAN_FIELD_TYPE_INTEGER,
      SU_TRUE,
      "lna_gain",
      "LNA gain"))
    return SU_FALSE;

  if (!suscan_source_add_field(
      source,
      SUSCAN_FIELD_TYPE_INTEGER,
      SU_TRUE,
      "bufsiz",
      "Buffer size"))
    return SU_FALSE;

  if (!suscan_source_add_field(
      source,
      SUSCAN_FIELD_TYPE_BOOLEAN,
      SU_FALSE,
      "amp",
      "Enable antenna amplifier"))
    return SU_FALSE;

  if (!suscan_source_add_field(
      source,
      SUSCAN_FIELD_TYPE_BOOLEAN,
      SU_FALSE,
      "bias",
      "Enable bias tee"))
    return SU_FALSE;

  return SU_TRUE;
}

#else
SUBOOL
suscan_hackRF_source_init(void)
{
  /* HackRF support disabled */
  return SU_TRUE;
}
#endif
