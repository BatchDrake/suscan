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

#ifndef _INSPECTOR_INTERFACE_H
#define _INSPECTOR_INTERFACE_H

#include <util.h>
#include <sigutils/sigutils.h>
#include <sigutils/specttuner.h>
#include <cfg.h>

#include "../estimator.h"
#include "../spectsrc.h"

struct suscan_inspector;

struct suscan_inspector_sampling_info {
  su_specttuner_channel_t *schan; /* Borrowed: specttuner channel */
  SUFLOAT equiv_fs;    /* Equivalent sample rate */
  SUFLOAT bw;          /* Bandwidth */
  SUFLOAT f0;
};


struct suscan_inspector_interface {
  const char *name;
  const char *desc;
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

#endif /* _INSPECTOR_INTERFACE_H */
