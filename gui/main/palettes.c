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

#include <stdlib.h>
#include <string.h>

#define SU_LOG_DOMAIN "gui-palettes"

#include <palettes.h>
#include <object.h>

void
suscan_gui_palette_destroy(suscan_gui_palette_t *pal)
{
  if (pal->name != NULL)
    free(pal->name);

  if (pal->thumbnail != NULL)
    free(pal->thumbnail);

  free(pal);
}

suscan_gui_palette_t *
suscan_gui_palette_new(const char *name)
{
  suscan_gui_palette_t *new = NULL;

  SU_TRYCATCH(new = calloc(1, sizeof(suscan_gui_palette_t)), goto fail);

  SU_TRYCATCH(new->name = strdup(name), goto fail);
  SU_TRYCATCH(
      new->thumbnail = calloc(
          3,
          SUSCAN_GUI_PALETTE_THUMB_WIDTH * SUSCAN_GUI_PALETTE_THUMB_HEIGHT),
      goto fail);

  return new;

fail:
  if (new != NULL)
    suscan_gui_palette_destroy(new);

  return NULL;
}

SUBOOL
suscan_gui_palette_add_stop(
    suscan_gui_palette_t *palette,
    unsigned int stop,
    float r, float g, float b)
{
  unsigned char bit, byte;

  SU_TRYCATCH(stop < SUSCAN_GUI_PALETTE_MAX_STOPS, return SU_FALSE);

  palette->gradient[stop][0] = r;
  palette->gradient[stop][1] = g;
  palette->gradient[stop][2] = b;

  byte = stop >> 3;
  bit  = stop & 7;

  palette->bitmap[byte] |= 1 << bit;

  return SU_TRUE;
}

void
suscan_gui_palette_compose(suscan_gui_palette_t *palette)
{
  unsigned char bit, byte;
  SUFLOAT alpha, c0, c1, r, g, b;
  int prev = -1;
  int i, j, index;

  for (i = 0; i < SUSCAN_GUI_PALETTE_MAX_STOPS; ++i) {
    byte = i >> 3;
    bit  = i & 7;

    if (palette->bitmap[byte] & (1 << bit)) {
      /* Used color found! */
      if (prev == -1) {
        /* This is the first stop. Fill from 0 until this one */
        for (j = 0; j < i; ++j) {
          palette->gradient[j][0] = palette->gradient[i][0];
          palette->gradient[j][1] = palette->gradient[i][1];
          palette->gradient[j][2] = palette->gradient[i][2];
        }
      } else {
        /* Not the first stop. perform square root mixing */
        for (j = prev + 1; j < i; ++j) {
          alpha = (float) (j - prev) / (float) (i - prev);

          c0 = palette->gradient[i][0];
          c1 = palette->gradient[prev][0];
          palette->gradient[j][0] =
              SU_SQRT(alpha * c0 * c0 + (1 - alpha) * c1 * c1);

          c0 = palette->gradient[i][1];
          c1 = palette->gradient[prev][1];
          palette->gradient[j][1] =
              SU_SQRT(alpha * c0 * c0 + (1 - alpha) * c1 * c1);

          c0 = palette->gradient[i][2];
          c1 = palette->gradient[prev][2];
          palette->gradient[j][2] =
              SU_SQRT(alpha * c0 * c0 + (1 - alpha) * c1 * c1);
        }
      }

      prev = i;
    }
  }

  if (prev != -1) {
    for (j = prev + 1; j < SUSCAN_GUI_PALETTE_MAX_STOPS; ++j) {
      palette->gradient[j][0] = palette->gradient[prev][0];
      palette->gradient[j][1] = palette->gradient[prev][1];
      palette->gradient[j][2] = palette->gradient[prev][2];
    }
  }

  /* Compose thumbnail */
  for (i = 0; i < SUSCAN_GUI_PALETTE_THUMB_WIDTH; ++i) {
    index = ((SUSCAN_GUI_PALETTE_MAX_STOPS - 1) * i)
        / (SUSCAN_GUI_PALETTE_THUMB_WIDTH - 1);

    r = 0xff * palette->gradient[index][0];
    g = 0xff * palette->gradient[index][1];
    b = 0xff * palette->gradient[index][2];

    for (j = 0; j < SUSCAN_GUI_PALETTE_THUMB_HEIGHT; ++j) {
      palette->thumbnail[3 * (SUSCAN_GUI_PALETTE_THUMB_WIDTH * j + i) + 0] = r;
      palette->thumbnail[3 * (SUSCAN_GUI_PALETTE_THUMB_WIDTH * j + i) + 1] = g;
      palette->thumbnail[3 * (SUSCAN_GUI_PALETTE_THUMB_WIDTH * j + i) + 2] = b;
    }
  }
}

/* Serialization and deserialization */
suscan_gui_palette_t *
suscan_gui_palette_deserialize(const suscan_object_t *object)
{
  const char *name;
  const suscan_object_t *stops, *entry;
  suscan_gui_palette_t *new = NULL;
  unsigned int i, count;
  int position;
  SUFLOAT red, green, blue;

  SU_TRYCATCH(
      name = suscan_object_get_field_value(object, "name"),
      return NULL);

  SU_TRYCATCH(
      stops = suscan_object_get_field(object, "stops"),
      return NULL);

  SU_TRYCATCH(
      suscan_object_get_type(stops) == SUSCAN_OBJECT_TYPE_SET,
      return NULL);

  SU_TRYCATCH(new = suscan_gui_palette_new(name), goto fail);

  /* Traverse stop list */
  count = suscan_object_set_get_count(stops);

  for (i = 0; i < count; ++i) {
    if ((entry = suscan_object_set_get(stops, i)) != NULL) {
      position = suscan_object_get_field_uint(entry, "position", -1);
      if (position < 0 || position > 255)
        continue;

      red = suscan_object_get_field_float(entry, "red", -1);
      if (red < 0 || red > 1)
        continue;
      green = suscan_object_get_field_float(entry, "green", -1);
      if (green < 0 || green > 1)
        continue;
      blue = suscan_object_get_field_float(entry, "blue", -1);
      if (blue < 0 || blue > 1)
        continue;

      SU_TRYCATCH(
          suscan_gui_palette_add_stop(new, position, red, green, blue),
          goto fail);
    }
  }

  suscan_gui_palette_compose(new);

  return new;

fail:
  if (new != NULL)
    suscan_gui_palette_destroy(new);

  return NULL;
}
