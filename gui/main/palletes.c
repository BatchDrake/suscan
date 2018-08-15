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
#define SU_LOG_DOMAIN "gui-palletes"

#include "palletes.h"

void
suscan_gui_pallete_destroy(suscan_gui_pallete_t *pal)
{
  if (pal->name != NULL)
    free(pal->name);

  if (pal->thumbnail != NULL)
    free(pal->thumbnail);

  free(pal);
}

suscan_gui_pallete_t *
suscan_gui_pallete_new(const char *name)
{
  suscan_gui_pallete_t *new = NULL;

  SU_TRYCATCH(new = calloc(1, sizeof(suscan_gui_pallete_t)), goto fail);

  SU_TRYCATCH(new->name = strdup(name), goto fail);
  SU_TRYCATCH(
      new->thumbnail = calloc(
          3,
          SUSCAN_GUI_PALLETE_THUMB_WIDTH * SUSCAN_GUI_PALLETE_THUMB_HEIGHT),
      goto fail);

  return new;

fail:
  if (new != NULL)
    suscan_gui_pallete_destroy(new);

  return NULL;
}

SUBOOL
suscan_gui_pallete_add_stop(
    suscan_gui_pallete_t *pallete,
    unsigned int stop,
    float r, float g, float b)
{
  unsigned char bit, byte;

  SU_TRYCATCH(stop < SUSCAN_GUI_PALLETE_MAX_STOPS, return SU_FALSE);

  pallete->gradient[stop][0] = r;
  pallete->gradient[stop][1] = g;
  pallete->gradient[stop][2] = b;

  byte = stop >> 3;
  bit  = stop & 7;

  pallete->bitmap[byte] |= 1 << bit;

  return SU_TRUE;
}

void
suscan_gui_pallete_compose(suscan_gui_pallete_t *pallete)
{
  unsigned char bit, byte;
  SUFLOAT alpha, c0, c1, r, g, b;
  int prev = -1;
  int i, j, index;

  for (i = 0; i < SUSCAN_GUI_PALLETE_MAX_STOPS; ++i) {
    byte = i >> 3;
    bit  = i & 7;

    if (pallete->bitmap[byte] & (1 << bit)) {
      /* Used color found! */
      if (prev == -1) {
        /* This is the first stop. Fill from 0 until this one */
        for (j = 0; j < i; ++j) {
          pallete->gradient[j][0] = pallete->gradient[i][0];
          pallete->gradient[j][1] = pallete->gradient[i][1];
          pallete->gradient[j][2] = pallete->gradient[i][2];
        }
      } else {
        /* Not the first stop. perform square root mixing */
        for (j = prev + 1; j < i; ++i) {
          alpha = (float) (j - prev) / (float) (i - prev);

          c0 = pallete->gradient[i][0];
          c1 = pallete->gradient[prev][0];
          pallete->gradient[j][0] =
              SU_SQRT(alpha * c0 * c0 + (1 - alpha) * c1 * c1);

          c0 = pallete->gradient[i][1];
          c1 = pallete->gradient[prev][1];
          pallete->gradient[j][1] =
              SU_SQRT(alpha * c0 * c0 + (1 - alpha) * c1 * c1);

          c0 = pallete->gradient[i][2];
          c1 = pallete->gradient[prev][2];
          pallete->gradient[j][2] =
              SU_SQRT(alpha * c0 * c0 + (1 - alpha) * c1 * c1);
        }
      }

      prev = i;
    }
  }

  if (prev != -1) {
    for (j = prev + 1; j < SUSCAN_GUI_PALLETE_MAX_STOPS; ++j) {
      pallete->gradient[j][0] = pallete->gradient[prev][0];
      pallete->gradient[j][1] = pallete->gradient[prev][1];
      pallete->gradient[j][2] = pallete->gradient[prev][2];
    }
  }

  /* Compose thumbnail */
  for (i = 0; i < SUSCAN_GUI_PALLETE_THUMB_WIDTH; ++i) {
    index = ((SUSCAN_GUI_PALLETE_MAX_STOPS - 1) * i)
        / (SUSCAN_GUI_PALLETE_THUMB_WIDTH - 1);

    r = 0xff * pallete->gradient[index][0];
    g = 0xff * pallete->gradient[index][1];
    b = 0xff * pallete->gradient[index][2];

    for (j = 0; j < SUSCAN_GUI_PALLETE_THUMB_HEIGHT; ++j) {
      pallete->thumbnail[3 * (SUSCAN_GUI_PALLETE_THUMB_WIDTH * j + i) + 0] = r;
      pallete->thumbnail[3 * (SUSCAN_GUI_PALLETE_THUMB_WIDTH * j + i) + 1] = g;
      pallete->thumbnail[3 * (SUSCAN_GUI_PALLETE_THUMB_WIDTH * j + i) + 2] = b;
    }
  }
}

