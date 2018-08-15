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

#ifndef _GUI_MAIN_PALLETES_H
#define _GUI_MAIN_PALLETES_H

#include <suscan.h>

#define SUSCAN_GUI_PALLETE_MAX_STOPS 256
#define SUSCAN_GUI_PALLETE_BITMAP_SZ ((SUSCAN_GUI_PALLETE_MAX_STOPS + 7) / 8)

#define SUSCAN_GUI_PALLETE_THUMB_WIDTH  64
#define SUSCAN_GUI_PALLETE_THUMB_HEIGHT 16

typedef float suscan_gradient_t[SUSCAN_GUI_PALLETE_MAX_STOPS][3];

struct suscan_gui_pallete {
  char *name;
  suscan_gradient_t gradient;
  unsigned char *thumbnail;
  char bitmap[SUSCAN_GUI_PALLETE_BITMAP_SZ];
};

typedef struct suscan_gui_pallete suscan_gui_pallete_t;

SUINLINE const char *
suscan_gui_pallete_get_name(const suscan_gui_pallete_t *pal)
{
  return pal->name;
}

SUINLINE const suscan_gradient_t *
suscan_gui_pallete_get_gradient(const suscan_gui_pallete_t *pallete)
{
  return &pallete->gradient;
}

SUINLINE unsigned char *
suscan_gui_pallete_get_thumbnail(const suscan_gui_pallete_t *pallete)
{
  return pallete->thumbnail;
}

void suscan_gui_pallete_destroy(suscan_gui_pallete_t *pal);
suscan_gui_pallete_t *suscan_gui_pallete_new(const char *name);
SUBOOL suscan_gui_pallete_add_stop(
    suscan_gui_pallete_t *pallete,
    unsigned int stop,
    float r, float g, float b);
void suscan_gui_pallete_compose(suscan_gui_pallete_t *pallete);

#endif /* _GUI_MAIN_PALLETES_H */
