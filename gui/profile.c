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

#define SU_LOG_DOMAIN "gui-profile"

#include "gui.h"

void
suscan_gui_profile_destroy(suscan_gui_profile_t *profile)
{
  if (profile->builder != NULL)
    g_object_unref(profile->builder);

  free(profile);
}

SUBOOL
suscan_gui_profile_refresh_config(suscan_gui_profile_t *profile)
{
  /* Move GUI fields to config */
  return SU_TRUE;
}

SUBOOL
suscan_gui_profile_refresh_gui(suscan_gui_profile_t *profile)
{
  /* Move config to GUI fields */
  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_gui_profile_load_all_widgets(suscan_gui_profile_t *profile)
{
  SU_TRYCATCH(
      profile->root =
          GTK_WIDGET(gtk_builder_get_object(
              profile->builder,
              "fRoot")),
      return SU_FALSE);

  SU_TRYCATCH(
      profile->selector =
          GTK_WIDGET(gtk_builder_get_object(
              profile->builder,
              "grSelector")),
      return SU_FALSE);

  return SU_TRUE;
}

suscan_gui_profile_t *
suscan_gui_profile_new(suscan_source_config_t *cfg)
{
  suscan_gui_profile_t *new = NULL;

  SU_TRYCATCH(new = calloc(1, sizeof (suscan_gui_profile_t)), goto fail);

  new->config = cfg;

  SU_TRYCATCH(
      new->builder = gtk_builder_new_from_file(PKGDATADIR "/gui/profile.glade"),
      goto fail);

  gtk_builder_connect_signals(new->builder, new);

  SU_TRYCATCH(suscan_gui_profile_load_all_widgets(new), goto fail);

  return new;

fail:
  if (new != NULL)
    suscan_gui_profile_destroy(new);

  return NULL;
}
