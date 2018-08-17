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

#define SU_LOG_DOMAIN "gui-wf-palettes"

#include <confdb.h>
#include "gui.h"

/*********************** Initialize palettes from config *********************/
suscan_gui_palette_t *
suscan_gui_lookup_palette(const suscan_gui_t *gui, const char *name)
{
  unsigned int i;

  for (i = 0; i < gui->palette_count; ++i)
    if (gui->palette_list[i] != NULL
        && strcmp(suscan_gui_palette_get_name(gui->palette_list[i]), name) == 0)
      return gui->palette_list[i];

  return NULL;
}

SUBOOL
suscan_gui_populate_pal_box(const suscan_gui_t *gui, SuGtkPalBox *palbox)
{
  unsigned int i;

  for (i = 0; i < gui->palette_count; ++i)
      if (gui->palette_list[i] != NULL)
        SU_TRYCATCH(
            sugtk_pal_box_append(palbox, gui->palette_list[i]),
            return SU_FALSE);

  return SU_TRUE;
}

SUBOOL
suscan_gui_load_palettes(suscan_gui_t *gui)
{
  suscan_config_context_t *ctx;
  suscan_gui_palette_t *palette = NULL;
  const suscan_object_t *list, *entry;
  const char *name;
  unsigned int i, count;

  /* If the palettes context exists: deserialize all palette objects */
  if ((ctx = suscan_config_context_lookup("palettes")) != NULL) {
    list = suscan_config_context_get_list(ctx);
    count = suscan_object_set_get_count(list);

    for (i = 0; i < count; ++i) {
      if ((entry = suscan_object_set_get(list, i)) != NULL) {
        name = suscan_object_get_field_value(entry, "name");
          if (name != NULL && suscan_gui_lookup_palette(gui, name) == NULL) {
          SU_TRYCATCH(
              palette = suscan_gui_palette_deserialize(entry),
              {
                SU_WARNING("Failed to deserialize palette, skipping\n");
                continue;
              });

          SU_TRYCATCH(
              PTR_LIST_APPEND_CHECK(gui->palette, palette) != -1,
              goto fail);

          palette = NULL;
        }
      }
    }
  }

  return SU_TRUE;

fail:
  if (palette != NULL)
    suscan_gui_palette_destroy(palette);

  return SU_FALSE;
}
