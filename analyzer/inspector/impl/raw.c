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

#define SU_LOG_DOMAIN "raw-inspector"

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
void *
suscan_raw_inspector_open(const struct suscan_inspector_sampling_info *s)
{
  return (void *) s;
}

SUBOOL
suscan_raw_inspector_get_config(void *private, suscan_config_t *config)
{
  return SU_TRUE;
}

SUBOOL
suscan_raw_inspector_parse_config(void *private, const suscan_config_t *config)
{
  return SU_TRUE;
}

/* Called inside inspector mutex */
void
suscan_raw_inspector_commit_config(void *private)
{
}

SUSDIFF
suscan_raw_inspector_feed(
    void *private,
    suscan_inspector_t *insp,
    const SUCOMPLEX *x,
    SUSCOUNT count)
{
  /* Pass-thru */
  return suscan_inspector_push_sample_buffer(insp, x, count);
}

void
suscan_raw_inspector_close(void *private)
{
}

SUPRIVATE struct suscan_inspector_interface iface = {
    .name = "raw",
    .desc = "Pass-through",
    .open = suscan_raw_inspector_open,
    .get_config = suscan_raw_inspector_get_config,
    .parse_config = suscan_raw_inspector_parse_config,
    .commit_config = suscan_raw_inspector_commit_config,
    .feed = suscan_raw_inspector_feed,
    .close = suscan_raw_inspector_close
};

SUBOOL
suscan_raw_inspector_register(void)
{
  SU_TRYCATCH(
      iface.cfgdesc = suscan_config_desc_new_ex(
          "raw-params-desc-" SUSCAN_VERSION_STRING),
      return SU_FALSE);

  SU_TRYCATCH(suscan_config_desc_register(iface.cfgdesc), return SU_FALSE);

  (void) suscan_inspector_interface_add_spectsrc(&iface, "psd");
  
  /* Register inspector interface */
  SU_TRYCATCH(suscan_inspector_interface_register(&iface), return SU_FALSE);

  return SU_TRUE;
}
