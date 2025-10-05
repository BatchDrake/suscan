/*

  Copyright (C) 2018 Gonzalo José Carracedo Carballal

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

#ifndef _INSPECTOR_INTERFACE_H
#define _INSPECTOR_INTERFACE_H

#include <sigutils/util/util.h>
#include <sigutils/sigutils.h>
#include <cfg.h>

#include "../estimator.h"
#include "../spectsrc.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct suscan_inspector;

struct suscan_inspector_sampling_info {
  SUFLOAT equiv_fs;         /* Equivalent sample rate */
  SUFLOAT bw;               /* Bandwidth */
  SUFLOAT bw_bd;            /* Bandwidth before decimation */
  SUFLOAT f0;               /* Center frequency */
  SUSCOUNT fft_size;        /* Size of the FFT window. */
  SUSCOUNT fft_bins;        /* Number of non-zero bins in the FFT window */
  SUBOOL   early_windowing; /* Early windowing is being applied */
  unsigned decimation;      /* Decimation */
};

struct suscan_inspector_interface {
  const char *name;               /* Name of this inspector interface */
  const char *desc;               /* Description */
  const char *sc_factory_class;   /* Factory class (if any) */

  SUBOOL frequency_domain;
  
  suscan_config_desc_t *cfgdesc;

  PTR_LIST_CONST(struct suscan_spectsrc_class, spectsrc);
  PTR_LIST_CONST(struct suscan_estimator_class, estimator);

  void *(*open) (const struct suscan_inspector_sampling_info *sinfo);

  /* Get current configuration */
  SUBOOL (*get_config) (void *priv, suscan_config_t *config);

  /* Parse config and store it in a temporary area */
  SUBOOL (*parse_config) (void *priv, const suscan_config_t *config);

  /* Adjust on new bandwidth */
  void (*new_bandwidth) (void *priv, SUFREQ bandwidth);

  /* Commit parsed config */
  void (*commit_config) (void *priv);

  /* Feed inspector with samples */
  SUSDIFF (*feed) (
      void *priv,
      struct suscan_inspector *insp,
      const SUCOMPLEX *x,
      SUSCOUNT count);

  /* Frequency was changed */
  void (*freq_changed) (
    void *priv,
    struct suscan_inspector *insp,
    SUFLOAT prev_freq,
    SUFLOAT next_freq);
  
  /* Close inspector */
  void (*close) (void *priv);
};

const struct suscan_inspector_interface *suscan_inspector_interface_lookup(
    const char *name);

SUBOOL suscan_inspector_interface_register(
    const struct suscan_inspector_interface *iface);

void suscan_inspector_interface_get_list(
    const struct suscan_inspector_interface ***iface_list,
    unsigned int *iface_count);

SUBOOL suscan_inspector_interface_add_spectsrc(
    struct suscan_inspector_interface *iface,
    const char *name);

SUBOOL suscan_inspector_interface_add_estimator(
    struct suscan_inspector_interface *iface,
    const char *name);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _INSPECTOR_INTERFACE_H */
