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

#ifndef _GUI_MAIN_PALETTES_H
#define _GUI_MAIN_PALETTES_H

#include <suscan.h>

#define SUSCAN_GUI_PALETTE_MAX_STOPS 256
#define SUSCAN_GUI_PALETTE_BITMAP_SZ ((SUSCAN_GUI_PALETTE_MAX_STOPS + 7) / 8)

#define SUSCAN_GUI_PALETTE_THUMB_WIDTH  64
#define SUSCAN_GUI_PALETTE_THUMB_HEIGHT 20

typedef float suscan_gradient_t[SUSCAN_GUI_PALETTE_MAX_STOPS][3];

struct suscan_gui_palette {
  char *name;
  suscan_gradient_t gradient;
  unsigned char *thumbnail;
  char bitmap[SUSCAN_GUI_PALETTE_BITMAP_SZ];
};

typedef struct suscan_gui_palette suscan_gui_palette_t;

SUINLINE const char *
suscan_gui_palette_get_name(const suscan_gui_palette_t *pal)
{
  return pal->name;
}

SUINLINE const suscan_gradient_t *
suscan_gui_palette_get_gradient(const suscan_gui_palette_t *palette)
{
  return &palette->gradient;
}

SUINLINE unsigned char *
suscan_gui_palette_get_thumbnail(const suscan_gui_palette_t *palette)
{
  return palette->thumbnail;
}

void suscan_gui_palette_destroy(suscan_gui_palette_t *pal);
suscan_gui_palette_t *suscan_gui_palette_new(const char *name);
SUBOOL suscan_gui_palette_add_stop(
    suscan_gui_palette_t *palette,
    unsigned int stop,
    float r, float g, float b);
void suscan_gui_palette_compose(suscan_gui_palette_t *palette);
suscan_gui_palette_t *suscan_gui_palette_deserialize(
    const suscan_object_t *object);

#endif /* _GUI_MAIN_PALETTES_H */
