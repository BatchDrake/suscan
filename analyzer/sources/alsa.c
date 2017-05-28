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

#include <config.h>
#include "source.h"

#include "alsa.h"

SUPRIVATE SUBOOL real_time = SU_TRUE;

void
alsa_state_destroy(struct alsa_state *state)
{
  if (state->handle != NULL)
    snd_pcm_close(state->handle);

  free(state);
}

struct alsa_state *
alsa_state_new(const struct alsa_params *params)
{
  struct alsa_state *new = NULL;
  snd_pcm_hw_params_t *hw_params = NULL;
  int err = 0;
  int dir;
  unsigned int rate;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(new = calloc(1, sizeof(struct alsa_state)), goto done);

  new->fc = params->fc;
  new->dc_remove = params->dc_remove;

  SU_TRYCATCH(
      (err = snd_pcm_open(
          &new->handle,
          params->device,
          SND_PCM_STREAM_CAPTURE,
          0)) >= 0,
      goto done);

  SU_TRYCATCH(
      (err = snd_pcm_hw_params_malloc(&hw_params)) >= 0,
      goto done);

  SU_TRYCATCH(
      (err = snd_pcm_hw_params_any(new->handle, hw_params)) >= 0,
      goto done);

  SU_TRYCATCH(
      (err = snd_pcm_hw_params_set_access(
          new->handle,
          hw_params,
          SND_PCM_ACCESS_RW_INTERLEAVED)) >= 0,
      goto done);

  SU_TRYCATCH(
      (err = snd_pcm_hw_params_set_format(
          new->handle,
          hw_params,
          SND_PCM_FORMAT_S16_LE)) >= 0,
      goto done);

  rate = params->samp_rate;

  SU_TRYCATCH(
      (err = snd_pcm_hw_params_set_rate_near(
          new->handle,
          hw_params,
          &rate,
          0)) >= 0,
      goto done);


  new->samp_rate = rate;

  SU_TRYCATCH(
      (err = snd_pcm_hw_params_set_channels(
          new->handle,
          hw_params,
          1)) >= 0,
      goto done);

  SU_TRYCATCH(
      (err = snd_pcm_hw_params(new->handle, hw_params)) >= 0,
      goto done);

  SU_TRYCATCH(
      (err = snd_pcm_prepare(new->handle)) >= 0,
      goto done);

  ok = SU_TRUE;

done:
  if (err < 0)
    SU_ERROR("ALSA source initialization failed: %s\n", snd_strerror(err));

  if (hw_params != NULL)
    snd_pcm_hw_params_free(hw_params);

  if (!ok && new != NULL) {
    alsa_state_destroy(new);
    new = NULL;
  }

  return new;
}

SUPRIVATE void
su_block_alsa_dtor(void *private)
{
  struct alsa_state *state = (struct alsa_state *) private;

  alsa_state_destroy(state);
}

SUPRIVATE SUBOOL
su_block_alsa_ctor(struct sigutils_block *block, void **private, va_list ap)
{
  struct alsa_state *state = NULL;
  struct alsa_params *params;

  params = va_arg(ap, struct alsa_params *);

  if ((state = alsa_state_new(params)) == NULL) {
    SU_ERROR("Create ALSA state failed\n");
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

  if (!su_block_set_property_ref(
      block,
      SU_PROPERTY_TYPE_BOOL,
      "real_time",
      &real_time)) {
    SU_ERROR("Expose real_time failed\n");
    goto fail;
  }

  *private = state;

  return SU_TRUE;

fail:
  if (state != NULL)
    alsa_state_destroy(state);

  return SU_FALSE;
}

SUPRIVATE SUSDIFF
su_block_alsa_acquire(
    void *priv,
    su_stream_t *out,
    unsigned int port_id,
    su_block_port_t *in)
{
  struct alsa_state *state = (struct alsa_state *) priv;
  SUSDIFF size;
  SUCOMPLEX samp;
  SUCOMPLEX *start;
  int i;
  int got;

  /* Get the number of complex samples to acquire */
  size = su_stream_get_contiguous(
      out,
      &start,
      MIN(out->size, ALSA_INTEGER_BUFFER_SIZE));

  got = snd_pcm_readi(state->handle, state->buffer, size);
  if (got > 0) {
    /*
     * ALSA does not seem to allow to read FLOAT64_LE directly. We have
     * to transform the integer samples manually
     */

    if (state->dc_remove) {
      for (i = 0; i < got; ++i) {
        samp = state->buffer[i] / 32768.0;
        start[i] = samp - state->last;
        state->last = samp;
      }
    } else {
      for (i = 0; i < got; ++i)
        start[i] = state->buffer[i] / 32768.0;
    }

    /* Increment position */
    if (su_stream_advance_contiguous(out, got) != got) {
      SU_ERROR("Unexpected size after su_stream_advance_contiguous\n");
      return -1;
    }
  } else  {
    SU_ERROR("ALSA read error: %s\n", snd_strerror(got));
    return SU_BLOCK_PORT_READ_ERROR_ACQUIRE;
  }

  return size;
}

SUPRIVATE struct sigutils_block_class su_block_class_ALSA = {
    "alsa", /* name */
    0,         /* in_size */
    1,         /* out_size */
    su_block_alsa_ctor,     /* constructor */
    su_block_alsa_dtor,     /* destructor */
    su_block_alsa_acquire, /* acquire */
};


SUPRIVATE su_block_t *
suscan_alsa_source_ctor(const struct suscan_source_config *config)
{
  struct alsa_params params = alsa_params_INITIALIZER;
  const struct suscan_field_value *value;

  if ((value = suscan_source_config_get_value(config, "device")) == NULL)
    return NULL;
  if (value->set && strlen(value->as_string) > 0)
    params.device = value->as_string;

  if ((value = suscan_source_config_get_value(config, "fs")) == NULL)
    return NULL;
  if (value->set)
    params.samp_rate = value->as_int;

  if ((value = suscan_source_config_get_value(config, "fc")) == NULL)
    return NULL;
  if (value->set)
    params.fc = value->as_int;

  if ((value = suscan_source_config_get_value(config, "dcfilt")) == NULL)
    return NULL;
  if (value->set)
    params.dc_remove = value->as_bool;

  return su_block_new("alsa", &params);
}

SUBOOL
suscan_alsa_source_init(void)
{
  struct suscan_source *source = NULL;

  if (!su_block_class_register(&su_block_class_ALSA))
    return SU_FALSE;

  if ((source = suscan_source_register(
      "alsa",
      "ALSA audio live capture",
      suscan_alsa_source_ctor)) == NULL)
    return SU_FALSE;

  if (!suscan_source_add_field(
      source,
      SUSCAN_FIELD_TYPE_STRING,
      SU_TRUE,
      "device",
      "Capture device"))
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
      SUSCAN_FIELD_TYPE_BOOLEAN,
      SU_TRUE,
      "dcfilt",
      "DC Remove"))
    return SU_FALSE;

  return SU_TRUE;
}
