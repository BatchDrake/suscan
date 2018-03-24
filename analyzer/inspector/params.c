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

#define SU_LOG_DOMAIN "insp-params"

#include <sigutils/log.h>
#include "params.h"

/*********************** Gain control params *********************************/
SUBOOL
suscan_config_desc_add_gc_params(suscan_config_desc_t *desc)
{
  SU_TRYCATCH(
      suscan_config_desc_add_field(
          desc,
          SUSCAN_FIELD_TYPE_BOOLEAN,
          SU_TRUE,
          "agc.enabled",
          "Automatic Gain Control is enabled"),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_config_desc_add_field(
          desc,
          SUSCAN_FIELD_TYPE_FLOAT,
          SU_TRUE,
          "agc.gain",
          "Manual gain (dB)"),
      return SU_FALSE);

  return SU_TRUE;
}

SUBOOL
suscan_inspector_gc_params_parse(
    struct suscan_inspector_gc_params *params,
    const suscan_config_t *config)
{
  struct suscan_field_value *value;

  SU_TRYCATCH(
      value = suscan_config_get_value(
          config,
          "agc.gain"),
      return SU_FALSE);

  SU_TRYCATCH(value->field->type == SUSCAN_FIELD_TYPE_FLOAT, return SU_FALSE);

  params->gc_gain = SU_MAG_RAW(value->as_float);

  SU_TRYCATCH(
      value = suscan_config_get_value(
          config,
          "agc.enabled"),
      return SU_FALSE);

  SU_TRYCATCH(value->field->type == SUSCAN_FIELD_TYPE_BOOLEAN, return SU_FALSE);

  params->gc_ctrl = value->as_bool
      ? SUSCAN_INSPECTOR_GAIN_CONTROL_AUTOMATIC
      : SUSCAN_INSPECTOR_GAIN_CONTROL_MANUAL;

  return SU_TRUE;
}

SUBOOL
suscan_inspector_gc_params_save(
    const struct suscan_inspector_gc_params *params,
    suscan_config_t *config)
{
  SU_TRYCATCH(
      suscan_config_set_float(
          config,
          "agc.gain",
          SU_DB_RAW(params->gc_gain)),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_config_set_bool(
          config,
          "agc.enabled",
          params->gc_ctrl == SUSCAN_INSPECTOR_GAIN_CONTROL_AUTOMATIC),
      return SU_FALSE);

  return SU_TRUE;
}

/*************************** Frequency control *******************************/
SUBOOL
suscan_config_desc_add_fc_params(suscan_config_desc_t *desc)
{
  SU_TRYCATCH(
      suscan_config_desc_add_field(
          desc,
          SUSCAN_FIELD_TYPE_INTEGER,
          SU_TRUE,
          "afc.costas-order",
          "Constellation order (Costas loop)"),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_config_desc_add_field(
          desc,
          SUSCAN_FIELD_TYPE_INTEGER,
          SU_TRUE,
          "afc.bits-per-symbol",
          "Bits per symbol"),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_config_desc_add_field(
          desc,
          SUSCAN_FIELD_TYPE_FLOAT,
          SU_TRUE,
          "afc.offset",
          "Carrier offset (Hz)"),
      return SU_FALSE);

  return SU_TRUE;
}

SUBOOL
suscan_inspector_fc_params_parse(
    struct suscan_inspector_fc_params *params,
    const suscan_config_t *config)
{
  struct suscan_field_value *value;

  SU_TRYCATCH(
      value = suscan_config_get_value(
          config,
          "afc.costas-order"),
      return SU_FALSE);

  SU_TRYCATCH(value->field->type == SUSCAN_FIELD_TYPE_INTEGER, return SU_FALSE);

  params->fc_ctrl = value->as_int;

  SU_TRYCATCH(
      value = suscan_config_get_value(
          config,
          "afc.offset"),
      return SU_FALSE);

  SU_TRYCATCH(value->field->type == SUSCAN_FIELD_TYPE_FLOAT, return SU_FALSE);

  params->fc_off = value->as_float;

  return SU_TRUE;
}

SUBOOL
suscan_inspector_fc_params_save(
    const struct suscan_inspector_fc_params *params,
    suscan_config_t *config)
{
  SU_TRYCATCH(
      suscan_config_set_integer(
          config,
          "afc.costas-order",
          params->fc_ctrl),
      return SU_FALSE);

  if (params->fc_ctrl != SUSCAN_INSPECTOR_CARRIER_CONTROL_MANUAL)
    SU_TRYCATCH(
        suscan_config_set_integer(
            config,
            "afc.bits-per-symbol",
            params->fc_ctrl),
        return SU_FALSE);

  SU_TRYCATCH(
      suscan_config_set_float(
          config,
          "afc.offset",
          params->fc_off),
      return SU_FALSE);

  return SU_TRUE;
}

/*************************** Matched filtering *******************************/
SUBOOL
suscan_config_desc_add_mf_params(suscan_config_desc_t *desc)
{
  SU_TRYCATCH(
      suscan_config_desc_add_field(
          desc,
          SUSCAN_FIELD_TYPE_INTEGER,
          SU_TRUE,
          "mf.type",
          "Matched filter configuration"),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_config_desc_add_field(
          desc,
          SUSCAN_FIELD_TYPE_FLOAT,
          SU_TRUE,
          "mf.roll-off",
          "Roll-off factor"),
      return SU_FALSE);

  return SU_TRUE;
}

SUBOOL
suscan_inspector_mf_params_parse(
    struct suscan_inspector_mf_params *params,
    const suscan_config_t *config)
{
  struct suscan_field_value *value;

  SU_TRYCATCH(
      value = suscan_config_get_value(
          config,
          "mf.type"),
      return SU_FALSE);

  SU_TRYCATCH(value->field->type == SUSCAN_FIELD_TYPE_INTEGER, return SU_FALSE);

  params->mf_conf = value->as_int;

  SU_TRYCATCH(
      value = suscan_config_get_value(
          config,
          "mf.roll-off"),
      return SU_FALSE);

  SU_TRYCATCH(value->field->type == SUSCAN_FIELD_TYPE_FLOAT, return SU_FALSE);

  params->mf_rolloff = value->as_float;

  return SU_TRUE;
}

SUBOOL
suscan_inspector_mf_params_save(
    const struct suscan_inspector_mf_params *params,
    suscan_config_t *config)
{
  SU_TRYCATCH(
      suscan_config_set_integer(
          config,
          "mf.type",
          params->mf_conf),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_config_set_float(
          config,
          "mf.roll-off",
          params->mf_rolloff),
      return SU_FALSE);

  return SU_TRUE;
}

/***************************** Equalization *********************************/
SUBOOL
suscan_config_desc_add_eq_params(suscan_config_desc_t *desc)
{
  SU_TRYCATCH(
      suscan_config_desc_add_field(
          desc,
          SUSCAN_FIELD_TYPE_INTEGER,
          SU_TRUE,
          "equalizer.type",
          "Equalizer configuration"),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_config_desc_add_field(
          desc,
          SUSCAN_FIELD_TYPE_FLOAT,
          SU_TRUE,
          "equalizer.rate",
          "Equalizer update rate"),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_config_desc_add_field(
          desc,
          SUSCAN_FIELD_TYPE_BOOLEAN,
          SU_TRUE,
          "equalizer.locked",
          "Equalizer has corrected channel distortion"),
      return SU_FALSE);

  return SU_TRUE;
}

SUBOOL
suscan_inspector_eq_params_parse(
    struct suscan_inspector_eq_params *params,
    const suscan_config_t *config)
{
  struct suscan_field_value *value;

  SU_TRYCATCH(
      value = suscan_config_get_value(
          config,
          "equalizer.type"),
      return SU_FALSE);

  SU_TRYCATCH(value->field->type == SUSCAN_FIELD_TYPE_INTEGER, return SU_FALSE);

  params->eq_conf = value->as_int;

  SU_TRYCATCH(
      value = suscan_config_get_value(
          config,
          "equalizer.rate"),
      return SU_FALSE);

  SU_TRYCATCH(value->field->type == SUSCAN_FIELD_TYPE_FLOAT, return SU_FALSE);

  params->eq_mu = value->as_float;

  SU_TRYCATCH(
      value = suscan_config_get_value(
          config,
          "equalizer.locked"),
      return SU_FALSE);

  SU_TRYCATCH(value->field->type == SUSCAN_FIELD_TYPE_BOOLEAN, return SU_FALSE);

  params->eq_locked = value->as_bool;

  return SU_TRUE;
}

SUBOOL
suscan_inspector_eq_params_save(
    const struct suscan_inspector_eq_params *params,
    suscan_config_t *config)
{
  SU_TRYCATCH(
      suscan_config_set_integer(
          config,
          "equalizer.type",
          params->eq_conf),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_config_set_float(
          config,
          "equalizer.rate",
          params->eq_mu),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_config_set_bool(
          config,
          "equalizer.locked",
          params->eq_locked),
      return SU_FALSE);

  return SU_TRUE;
}

/**************************** Clock recovery *********************************/
SUBOOL
suscan_config_desc_add_br_params(suscan_config_desc_t *desc)
{
  SU_TRYCATCH(
      suscan_config_desc_add_field(
          desc,
          SUSCAN_FIELD_TYPE_INTEGER,
          SU_TRUE,
          "clock.type",
          "Clock recovery method"),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_config_desc_add_field(
          desc,
          SUSCAN_FIELD_TYPE_FLOAT,
          SU_TRUE,
          "clock.baud",
          "Symbol rate (baud)"),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_config_desc_add_field(
          desc,
          SUSCAN_FIELD_TYPE_FLOAT,
          SU_TRUE,
          "clock.gain",
          "Gardner's algorithm loop gain"),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_config_desc_add_field(
          desc,
          SUSCAN_FIELD_TYPE_FLOAT,
          SU_TRUE,
          "clock.phase",
          "Symbol phase"),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_config_desc_add_field(
          desc,
          SUSCAN_FIELD_TYPE_BOOLEAN,
          SU_TRUE,
          "clock.running",
          "Clock recovery is running"),
      return SU_FALSE);

  return SU_TRUE;
}

SUBOOL
suscan_inspector_br_params_parse(
    struct suscan_inspector_br_params *params,
    const suscan_config_t *config)
{
  struct suscan_field_value *value;

  SU_TRYCATCH(
      value = suscan_config_get_value(
          config,
          "clock.type"),
      return SU_FALSE);

  SU_TRYCATCH(value->field->type == SUSCAN_FIELD_TYPE_INTEGER, return SU_FALSE);

  params->br_ctrl = value->as_int;

  SU_TRYCATCH(
      value = suscan_config_get_value(
          config,
          "clock.gain"),
      return SU_FALSE);

  SU_TRYCATCH(value->field->type == SUSCAN_FIELD_TYPE_FLOAT, return SU_FALSE);

  params->br_alpha = SU_MAG_RAW(value->as_float);

  SU_TRYCATCH(
      value = suscan_config_get_value(
          config,
          "clock.baud"),
      return SU_FALSE);

  SU_TRYCATCH(value->field->type == SUSCAN_FIELD_TYPE_FLOAT, return SU_FALSE);

  params->baud = value->as_float;

  SU_TRYCATCH(
      value = suscan_config_get_value(
          config,
          "clock.phase"),
      return SU_FALSE);

  SU_TRYCATCH(value->field->type == SUSCAN_FIELD_TYPE_FLOAT, return SU_FALSE);

  params->sym_phase = value->as_float;

  SU_TRYCATCH(
      value = suscan_config_get_value(
          config,
          "clock.running"),
      return SU_FALSE);

  SU_TRYCATCH(value->field->type == SUSCAN_FIELD_TYPE_BOOLEAN, return SU_FALSE);

  params->br_running = value->as_bool;

  return SU_TRUE;
}

SUBOOL
suscan_inspector_br_params_save(
    const struct suscan_inspector_br_params *params,
    suscan_config_t *config)
{
  SU_TRYCATCH(
      suscan_config_set_integer(
          config,
          "clock.type",
          params->br_ctrl),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_config_set_float(
          config,
          "clock.gain",
          SU_DB_RAW(params->br_alpha)),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_config_set_float(
          config,
          "clock.baud",
          params->baud),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_config_set_float(
          config,
          "clock.phase",
          params->sym_phase),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_config_set_bool(
          config,
          "clock.running",
          params->br_running),
      return SU_FALSE);

  return SU_TRUE;
}

