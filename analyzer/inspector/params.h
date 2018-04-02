/*

  Copyright (C) 2018 Gonzalo José Carracedo Carballal

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

#ifndef _INSPECTOR_PARAMS_H
#define _INSPECTOR_PARAMS_H

#include <sigutils/types.h>

#include <cfg.h>

/*********************** Gain control params *********************************/
enum suscan_inspector_gain_control {
  SUSCAN_INSPECTOR_GAIN_CONTROL_MANUAL,
  SUSCAN_INSPECTOR_GAIN_CONTROL_AUTOMATIC
};

struct suscan_inspector_gc_params {
  enum suscan_inspector_gain_control gc_ctrl;
  SUFLOAT gc_gain;    /* Positive gain (linear) */
};

SUBOOL suscan_config_desc_add_gc_params(suscan_config_desc_t *desc);
SUBOOL suscan_inspector_gc_params_parse(
    struct suscan_inspector_gc_params *params,
    const suscan_config_t *config);
SUBOOL suscan_inspector_gc_params_save(
    const struct suscan_inspector_gc_params *params,
    suscan_config_t *config);

/*************************** Frequency control *******************************/
enum suscan_inspector_carrier_control {
  SUSCAN_INSPECTOR_CARRIER_CONTROL_MANUAL,
  SUSCAN_INSPECTOR_CARRIER_CONTROL_COSTAS_2,
  SUSCAN_INSPECTOR_CARRIER_CONTROL_COSTAS_4,
  SUSCAN_INSPECTOR_CARRIER_CONTROL_COSTAS_8,
};

struct suscan_inspector_fc_params {
  enum suscan_inspector_carrier_control fc_ctrl;
  SUFLOAT fc_off;     /* Offset frequency */
  SUFLOAT fc_phi;     /* Carrier phase */
  SUFLOAT fc_loopbw;  /* Loop bandwidth */
};

SUBOOL suscan_config_desc_add_fc_params(suscan_config_desc_t *desc);
SUBOOL suscan_inspector_fc_params_parse(
    struct suscan_inspector_fc_params *params,
    const suscan_config_t *config);
SUBOOL suscan_inspector_fc_params_save(
    const struct suscan_inspector_fc_params *params,
    suscan_config_t *config);

/*************************** Matched filtering *******************************/
enum suscan_inspector_matched_filter {
  SUSCAN_INSPECTOR_MATCHED_FILTER_BYPASS,
  SUSCAN_INSPECTOR_MATCHED_FILTER_MANUAL
};

struct suscan_inspector_mf_params {
  enum suscan_inspector_matched_filter mf_conf;
  SUFLOAT mf_rolloff; /* Roll-off factor */
};

SUBOOL suscan_config_desc_add_mf_params(suscan_config_desc_t *desc);
SUBOOL suscan_inspector_mf_params_parse(
    struct suscan_inspector_mf_params *params,
    const suscan_config_t *config);
SUBOOL suscan_inspector_mf_params_save(
    const struct suscan_inspector_mf_params *params,
    suscan_config_t *config);

/***************************** Equalization *********************************/
enum suscan_inspector_equalizer {
  SUSCAN_INSPECTOR_EQUALIZER_BYPASS,
  SUSCAN_INSPECTOR_EQUALIZER_CMA
};

struct suscan_inspector_eq_params {
  enum suscan_inspector_equalizer eq_conf;
  SUFLOAT eq_mu; /* Mu (learn speed) */
  SUBOOL  eq_locked; /* Locked (equivalent to setting mu to 0) */
};

SUBOOL suscan_config_desc_add_eq_params(suscan_config_desc_t *desc);
SUBOOL suscan_inspector_eq_params_parse(
    struct suscan_inspector_eq_params *params,
    const suscan_config_t *config);
SUBOOL suscan_inspector_eq_params_save(
    const struct suscan_inspector_eq_params *params,
    suscan_config_t *config);

/**************************** Clock recovery *********************************/
enum suscan_inspector_baudrate_control {
  SUSCAN_INSPECTOR_BAUDRATE_CONTROL_MANUAL,
  SUSCAN_INSPECTOR_BAUDRATE_CONTROL_GARDNER
};

struct suscan_inspector_br_params {
  enum suscan_inspector_baudrate_control br_ctrl;
  SUFLOAT baud;       /* Current baudrate config */
  SUFLOAT sym_phase;  /* Symbol phase */
  SUFLOAT br_alpha;   /* Baudrate control alpha (linear) */
  SUFLOAT br_beta;    /* Baudrate control beta (linear) */
  SUBOOL  br_running; /* Sampler enabled */
};

SUBOOL suscan_config_desc_add_br_params(suscan_config_desc_t *desc);
SUBOOL suscan_inspector_br_params_parse(
    struct suscan_inspector_br_params *params,
    const suscan_config_t *config);
SUBOOL suscan_inspector_br_params_save(
    const struct suscan_inspector_br_params *params,
    suscan_config_t *config);

/****************************** FSK config ***********************************/
struct suscan_inspector_fsk_params {
  unsigned int bits_per_tone;
};

SUBOOL suscan_config_desc_add_fsk_params(suscan_config_desc_t *desc);
SUBOOL suscan_inspector_fsk_params_parse(
    struct suscan_inspector_fsk_params *params,
    const suscan_config_t *config);
SUBOOL suscan_inspector_fsk_params_save(
    const struct suscan_inspector_fsk_params *params,
    suscan_config_t *config);


#endif /* _INSPECTOR_PARAMS_H */
