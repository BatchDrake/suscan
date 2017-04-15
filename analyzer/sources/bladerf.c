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

#ifdef HAVE_BLADERF

#include <string.h>

#include "bladerf.h"

SUPRIVATE SUBOOL real_time = SU_TRUE;

SUPRIVATE void
bladeRF_state_destroy(struct bladeRF_state *state)
{
  if (state->dev != NULL)
    bladerf_close(state->dev);

  if (state->buffer != NULL)
    free(state->buffer);

  free(state);
}

SUPRIVATE SUBOOL
bladeRF_state_init_sync(struct bladeRF_state *state)
{
  int status;

  status = bladerf_sync_config(
      state->dev,
      BLADERF_MODULE_RX,
      BLADERF_FORMAT_SC16_Q11,
      16,
      state->params.bufsiz,
      8,
      3500);
  if (status != 0) {
    SU_ERROR(
        "Failed to configure RX sync interface: %s\n",
        bladerf_strerror(status));
    return SU_FALSE;
  }

  return SU_TRUE;
}

SUPRIVATE struct bladeRF_state *
bladeRF_state_new(const struct bladeRF_params *params)
{
  struct bladeRF_state *new = NULL;
  struct bladerf_devinfo dev_info;
  unsigned int actual_samp_rate;
  unsigned int actual_fc;

  bladerf_xb300_amplifier amp;
  int status;

  if ((new = calloc(1, sizeof (struct bladeRF_state))) == NULL)
    goto fail;

  new->params = *params;

  /* 1 sample: 2 components (I & Q) */
  if ((new->buffer = malloc(sizeof(uint16_t) * params->bufsiz * 2)) == NULL)
    goto fail;

  bladerf_init_devinfo(&dev_info);

  if (params->serial != NULL) {
    strncpy(
        dev_info.serial,
        params->serial,
        sizeof(dev_info.serial) - 1);
    dev_info.serial[sizeof(dev_info.serial) - 1] = '\0';
  }

  /* Open BladeRF */
  status = bladerf_open_with_devinfo(&new->dev, &dev_info);
  if (status == BLADERF_ERR_NODEV) {
    if (params->serial != NULL)
      SU_ERROR("No bladeRF devices with serial %s\n", params->serial);
    else
      SU_ERROR("No available bladeRF devices found\n");

    goto fail;
  } else if (status != 0) {
    SU_ERROR("Cannot open device: %s\n", bladerf_strerror(status));
    goto fail;
  }

  /* Configure center frequency */
  if (params->fc != 0) {
    status = bladerf_set_frequency(new->dev, BLADERF_MODULE_RX, params->fc);
    if (status != 0) {
      SU_ERROR("Cannot set frequency: %s\n", bladerf_strerror(status));
      goto fail;
    }
  }

  status = bladerf_get_frequency(new->dev, BLADERF_MODULE_RX, &actual_fc);
  if (status != 0) {
    SU_ERROR("Failed to get frequency: %s\n", bladerf_strerror(status));
    goto fail;
  }

  /* Configure sample rate */
  if (params->samp_rate != 0) {
    status = bladerf_set_sample_rate(
        new->dev,
        BLADERF_MODULE_RX,
        params->samp_rate,
        &actual_samp_rate);
    if (status != 0) {
      SU_ERROR("Cannot enable RX module: %s\n", bladerf_strerror(status));
      goto fail;
    }
  } else {
    status = bladerf_get_sample_rate(
        new->dev,
        BLADERF_MODULE_RX,
        &actual_samp_rate);
    if (status != 0) {
      SU_ERROR("Cannot enable RX module: %s\n", bladerf_strerror(status));
      goto fail;
    }
  }

  /* Enable XB-300, if present */
  status = bladerf_expansion_attach(new->dev, BLADERF_XB_300);
  if (status == 0) {
    /* XB-300 found, enable or disable LNA accordingly */
    amp = BLADERF_XB300_AMP_LNA;

    status = bladerf_xb300_set_amplifier_enable(new->dev, amp, params->lna);
    if (status != 0) {
      SU_ERROR("Cannot enable XB-300: %s\n", bladerf_strerror(status));
      goto fail;
    }
  } else if (params->lna) {
    SU_WARNING("Cannot enable LNA: XB-300 found\n");
  }

  /* Configure VGA1, VGA2 and LNA gains */
  status = bladerf_set_rxvga1(new->dev, params->vga1);
  if (status != 0) {
    SU_ERROR("Failed to set VGA1 gain: %s\n", bladerf_strerror(status));
    goto fail;
  }

  status = bladerf_set_rxvga2(new->dev, params->vga2);
  if (status != 0) {
    SU_ERROR("Failed to set VGA2 gain: %s\n", bladerf_strerror(status));
    goto fail;
  }

  status = bladerf_set_lna_gain(new->dev, params->lnagain);
  if (status != 0) {
    SU_ERROR("Failed to set LNA gain: %s\n", bladerf_strerror(status));
    goto fail;
  }

  /* Configure sync RX */
  if (!bladeRF_state_init_sync(new)) {
    SU_ERROR("Failed to init bladeRF in sync mode\n");
    goto fail;
  }

  /* Enable RX */
  status = bladerf_enable_module(new->dev, BLADERF_MODULE_RX, SU_TRUE);
  if (status != 0) {
    SU_ERROR("Cannot enable RX module: %s\n", bladerf_strerror(status));
    goto fail;
  }

  new->samp_rate = actual_samp_rate;
  new->fc = actual_fc;

  return new;

fail:
  if (new != NULL)
    bladeRF_state_destroy(new);

  return NULL;
}

SUPRIVATE void
su_block_bladeRF_dtor(void *private)
{
  struct bladeRF_state *state = (struct bladeRF_state *) private;

  bladeRF_state_destroy(state);
}

SUPRIVATE SUBOOL
su_block_bladeRF_ctor(struct sigutils_block *block, void **private, va_list ap)
{
  struct bladeRF_state *state = NULL;
  struct bladeRF_params *params;

  params = va_arg(ap, struct bladeRF_params *);

  if ((state = bladeRF_state_new(params)) == NULL) {
    SU_ERROR("Create bladeRF state failed\n");
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
    bladeRF_state_destroy(state);

  return SU_FALSE;
}

#ifdef BLADERF_SAVE_SAMPLES
FILE *fp;
#endif /* BLADERF_SAVE_SAMPLES */

SUPRIVATE SUSDIFF
su_block_bladeRF_acquire(
    void *priv,
    su_stream_t *out,
    unsigned int port_id,
    su_block_port_t *in)
{
  struct bladeRF_state *state = (struct bladeRF_state *) priv;
  SUSDIFF size;
  unsigned int i;
  SUCOMPLEX *start;
  SUCOMPLEX samp;
  int status;
#ifdef BLADERF_SAVE_SAMPLES
  complex float iq;

  if (fp == NULL)
    fp = fopen("output.raw", "wb");
#endif /* BLADERF_SAVE_SAMPLES */

  /* Get the number of complex samples to acquire */
  size = su_stream_get_contiguous(
      out,
      &start,
      SU_MIN(state->params.bufsiz, out->size));

  status = bladerf_sync_rx(
      state->dev,
      state->buffer,
      size,
      NULL,
      5000);
  if (status == 0) {
    /* Read OK. Transform samples */
      for (i = 0; i < size; ++i) {
        start[i] =
            state->buffer[i << 1] / 2048.0
            + I * state->buffer[(i << 1) + 1] / 2048.0;
#ifdef BLADERF_SAVE_SAMPLES
        iq = start[i];
        fwrite(&iq, 1, sizeof(complex float), fp);
#endif /* BLADERF_SAVE_SAMPLES */
      }

      /* Increment position */
      if (su_stream_advance_contiguous(out, size) != size) {
        SU_ERROR("Unexpected size after su_stream_advance_contiguous\n");
        return -1;
      }
  } else  {
    SU_ERROR("bladeRF sync read error: %s\n", bladerf_strerror(status));
    return SU_BLOCK_PORT_READ_ERROR_ACQUIRE;
  }

  return size;

}

SUPRIVATE struct sigutils_block_class su_block_class_BLADERF = {
    "bladeRF", /* name */
    0,         /* in_size */
    1,         /* out_size */
    su_block_bladeRF_ctor,     /* constructor */
    su_block_bladeRF_dtor,     /* destructor */
    su_block_bladeRF_acquire, /* acquire */
};

SUPRIVATE su_block_t *
suscan_bladeRF_source_ctor(const struct suscan_source_config *config)
{
  struct bladeRF_params params = sigutils_bladeRF_params_INITIALIZER;
  const struct suscan_field_value *value;

  if ((value = suscan_source_config_get_value(config, "serial")) == NULL)
    return NULL;
  if (value->set)
    params.serial = value->as_string;

  if ((value = suscan_source_config_get_value(config, "lna")) == NULL)
    return NULL;
  if (value->set)
    params.lna = value->as_bool;

  if ((value = suscan_source_config_get_value(config, "fs")) == NULL)
    return NULL;
  if (value->set)
    params.samp_rate = value->as_int;

  if ((value = suscan_source_config_get_value(config, "fc")) == NULL)
    return NULL;
  if (value->set)
    params.fc = value->as_int;

  if ((value = suscan_source_config_get_value(config, "bufsiz")) == NULL)
    return NULL;
  if (value->set)
    params.bufsiz = value->as_int;

  if ((value = suscan_source_config_get_value(config, "vga1")) == NULL)
    return NULL;
  if (value->set)
    params.vga1 = value->as_int;

  if ((value = suscan_source_config_get_value(config, "vga2")) == NULL)
    return NULL;
  if (value->set)
    params.vga2 = value->as_int;

  if ((value = suscan_source_config_get_value(config, "lnagain")) == NULL)
    return NULL;
  if (value->set)
    params.lnagain = value->as_int;

  return su_block_new("bladeRF", &params);
}

SUBOOL
suscan_bladeRF_source_init(void)
{
  struct suscan_source *source = NULL;

  if (!su_block_class_register(&su_block_class_BLADERF))
    return SU_FALSE;

  if ((source = suscan_source_register(
      "bladeRF",
      "Nuand's bladeRF SDR",
      suscan_bladeRF_source_ctor)) == NULL)
    return SU_FALSE;

  if (!suscan_source_add_field(
      source,
      SUSCAN_FIELD_TYPE_FILE,
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
      "bufsiz",
      "Buffer size"))
    return SU_FALSE;

  if (!suscan_source_add_field(
      source,
      SUSCAN_FIELD_TYPE_INTEGER,
      SU_TRUE,
      "vga1",
      "VGA1 gain"))
    return SU_FALSE;

  if (!suscan_source_add_field(
      source,
      SUSCAN_FIELD_TYPE_INTEGER,
      SU_TRUE,
      "vga2",
      "VGA2 gain"))
    return SU_FALSE;

  if (!suscan_source_add_field(
      source,
      SUSCAN_FIELD_TYPE_BOOLEAN,
      SU_FALSE,
      "lna",
      "Use XB-300 LNA"))
    return SU_FALSE;

  if (!suscan_source_add_field(
      source,
      SUSCAN_FIELD_TYPE_INTEGER,
      SU_TRUE,
      "lnagain",
      "LNA gain"))
    return SU_FALSE;


  return SU_TRUE;
}

#else
SUBOOL
suscan_bladeRF_source_init(void)
{
  /* BladeRF support disabled */
  return SU_TRUE;
}
#endif
