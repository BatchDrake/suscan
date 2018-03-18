/*

  Copyright (C) 2017 Gonzalo José Carracedo Carballal

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

#ifndef _GUI_LCD_H
#define _GUI_LCD_H

#include <glib-object.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define SUGTK_TYPE_LCD            (sugtk_lcd_get_type ())
#define SUGTK_LCD(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SUGTK_TYPE_LCD, SuGtkLcd))
#define SUGTK_LCD_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST  ((klass), SUGTK_TYPE_LCD, SuGtkLcdClass))
#define SUGTK_IS_LCD(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SUGTK_TYPE_LCD))
#define SUGTK_IS_LCD_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE  ((klass), SUGTK_TYPE_LCD))
#define SUGTK_LCD_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS  ((obj), SUGTK_TYPE_LCD, SuGtkLcdClass))

#define SUGTK_LCD_SEG_TOP          1
#define SUGTK_LCD_SEG_MIDDLE       2
#define SUGTK_LCD_SEG_BOTTOM       4

#define SUGTK_LCD_SEG_ALL_H        7

#define SUGTK_LCD_SEG_TOP_LEFT     8
#define SUGTK_LCD_SEG_BOTTOM_LEFT  16
#define SUGTK_LCD_SEG_TOP_RIGHT    32
#define SUGTK_LCD_SEG_BOTTOM_RIGHT 64

#define SUGTK_LCD_SEG_ALL_V        120

struct _SuGtkLcd
{
  GtkDrawingArea parent_instance;
  cairo_surface_t *sf_glyphs[10];
  cairo_surface_t *sf_display;

  gfloat width;
  gfloat height;

  gfloat glyph_width;
  gfloat glyph_height;

  gfloat thickness;
  gfloat padding;
  gfloat size;
  guint  length;
  gulong value;

  gfloat fg_r;
  gfloat fg_g;
  gfloat fg_b;

  gfloat bg_r;
  gfloat bg_g;
  gfloat bg_b;
};

struct _SuGtkLcdClass
{
  GtkDrawingAreaClass parent_class;
};

typedef struct _SuGtkLcd      SuGtkLcd;
typedef struct _SuGtkLcdClass SuGtkLcdClass;

GType sugtk_lcd_get_type(void);
GtkWidget *sugtk_lcd_new(void);

void sugtk_lcd_set_value(SuGtkLcd *lcd, gulong value);

G_END_DECLS

#endif /* _GUI_LCD_H */

