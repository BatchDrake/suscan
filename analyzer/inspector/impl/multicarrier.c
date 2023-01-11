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

#define SU_LOG_DOMAIN "multicarrier-inspector"

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

/************************** API implementation *******************************/
SUPRIVATE void
suscan_inspector_multicarrier_params_initialize(
  struct suscan_inspector_multicarrier_params *self,
  const struct suscan_inspector_sampling_info *sinfo)
{
  self->enabled = SU_TRUE;  
}

SUPRIVATE void
suscan_inspector_multicarrier_params_finalize(
  struct suscan_inspector_multicarrier_params *self)
{
}

void *
suscan_multicarrier_inspector_open(const struct suscan_inspector_sampling_info *s)
{
  struct suscan_inspector_multicarrier_params *params = NULL;

  /* This inspector is special in that it only allocates space for its parameters */

  SU_ALLOCATE_FAIL(params, struct suscan_inspector_multicarrier_params);
  
  suscan_inspector_multicarrier_params_initialize(params, s);

  return params;

fail:
  if (params != NULL) {
    suscan_inspector_multicarrier_params_finalize(params);
    free(params);
  }

  return NULL;
}

SUBOOL
suscan_multicarrier_inspector_get_config(void *private, suscan_config_t *config)
{
  struct suscan_inspector_multicarrier_params *params = 
    (struct suscan_inspector_multicarrier_params *) private;
  
  SU_TRYCATCH(
    suscan_inspector_multicarrier_params_save(params, config),
    return SU_FALSE);

  return SU_TRUE;
}

SUBOOL
suscan_multicarrier_inspector_parse_config(void *private, const suscan_config_t *config)
{
  struct suscan_inspector_multicarrier_params *params = 
    (struct suscan_inspector_multicarrier_params *) private;

  SU_TRYCATCH(
    suscan_inspector_multicarrier_params_parse(params, config),
    return SU_FALSE);

  return SU_TRUE;
}

/* Called inside inspector mutex */
void
suscan_multicarrier_inspector_commit_config(void *private)
{
}

SUSDIFF
suscan_multicarrier_inspector_feed(
    void *private,
    suscan_inspector_t *insp,
    const SUCOMPLEX *x,
    SUSCOUNT count)
{
  struct suscan_inspector_multicarrier_params *params = 
    (struct suscan_inspector_multicarrier_params *) private;
  
  if (params->enabled) {
    if (!suscan_inspector_feed_sc_stuner(insp, x, count)) {
      SU_ERROR("Failed to feed samples to subcarrier\n");
      return 0;
    }
  }

  return count;
}

void
suscan_multicarrier_inspector_close(void *private)
{
  struct suscan_inspector_multicarrier_params *params = 
    (struct suscan_inspector_multicarrier_params *) private;

  suscan_inspector_multicarrier_params_finalize(params);
  free(params);
}

SUPRIVATE struct suscan_inspector_interface iface = {
    .name = "multicarrier",
    .desc = "Multicarrier channel inspector",
    .sc_factory_class = "sc-inspector",
    .open = suscan_multicarrier_inspector_open,
    .get_config = suscan_multicarrier_inspector_get_config,
    .parse_config = suscan_multicarrier_inspector_parse_config,
    .commit_config = suscan_multicarrier_inspector_commit_config,
    .feed = suscan_multicarrier_inspector_feed,
    .close = suscan_multicarrier_inspector_close
};

SUBOOL
suscan_multicarrier_inspector_register(void)
{
  SU_TRYCATCH(
      iface.cfgdesc = suscan_config_desc_new_ex(
          "multicarrier-params-desc-" SUSCAN_VERSION_STRING),
      return SU_FALSE);

  SU_TRYCATCH(
    suscan_config_desc_add_multicarrier_params(iface.cfgdesc),
    return SU_FALSE);

  SU_TRYCATCH(suscan_config_desc_register(iface.cfgdesc), return SU_FALSE);

  /* Register inspector interface */
  SU_TRYCATCH(suscan_inspector_interface_register(&iface), return SU_FALSE);

  return SU_TRUE;
}
