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

#include <string.h>

#define SU_LOG_DOMAIN "gui"

#include <confdb.h>
#include "modemctl.h"
#include "gui.h"

SUBOOL
suscan_gui_helper_preload(void)
{
  SU_TRYCATCH(suscan_gui_modemctl_agc_init(), return SU_FALSE);
  SU_TRYCATCH(suscan_gui_modemctl_afc_init(), return SU_FALSE);
  SU_TRYCATCH(suscan_gui_modemctl_fsk_init(), return SU_FALSE);
  SU_TRYCATCH(suscan_gui_modemctl_mf_init(), return SU_FALSE);
  SU_TRYCATCH(suscan_gui_modemctl_equalizer_init(), return SU_FALSE);
  SU_TRYCATCH(suscan_gui_modemctl_clock_init(), return SU_FALSE);

  return SU_TRUE;
}

SUBOOL
suscan_gui_start(
    int argc,
    char **argv,
    struct suscan_source_config **config_list,
    unsigned int config_count)
{
  suscan_gui_t *gui = NULL;

  SU_TRYCATCH(suscan_graphical_init(argc, argv), goto fail);

  SU_TRYCATCH(suscan_confdb_use("gtkui"), goto fail);
  SU_TRYCATCH(suscan_confdb_use("demod"), goto fail);

  SU_TRYCATCH(suscan_gui_helper_preload(), goto fail);

  SU_TRYCATCH(gui = suscan_gui_new(), goto fail);

  gtk_widget_show(GTK_WIDGET(gui->main));

  suscan_gui_setup_logging(gui);

  SU_INFO("SUScan GTK interface initialized\n");

  gtk_main();

  return SU_TRUE;

fail:
  if (gui != NULL)
    suscan_gui_destroy(gui);

  return SU_FALSE;
}

