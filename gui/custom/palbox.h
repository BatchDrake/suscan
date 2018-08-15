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

#ifndef _GUI_PALBOX_H
#define _GUI_PALBOX_H

#include <glib-object.h>
#include <gtk/gtk.h>
#include <palettes.h>

G_BEGIN_DECLS

#define SUGTK_TYPE_PAL_BOX            (sugtk_pal_box_get_type ())
#define SUGTK_PAL_BOX(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SUGTK_TYPE_PAL_BOX, SuGtkPalBox))
#define SUGTK_PAL_BOX_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST  ((klass), SUGTK_TYPE_PAL_BOX, SuGtkPalBoxClass))
#define SUGTK_IS_PAL_BOX(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SUGTK_TYPE_PAL_BOX))
#define SUGTK_IS_PAL_BOX_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE  ((klass), SUGTK_TYPE_PAL_BOX))
#define SUGTK_PAL_BOX_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS  ((obj), SUGTK_TYPE_PAL_BOX, SuGtkPalBoxClass))

#define SUGTK_PAL_BOX_THUMB_WIDTH  SUSCAN_GUI_PALETTE_THUMB_WIDTH
#define SUGTK_PAL_BOX_THUMB_HEIGHT SUSCAN_GUI_PALETTE_THUMB_HEIGHT

struct _SuGtkPalBox
{
  GtkComboBox parent_instance;
  suscan_gui_palette_t *def_pal;
  GtkListStore *store;
};

struct _SuGtkPalBoxClass
{
  GtkComboBoxClass parent_class;
};

typedef struct _SuGtkPalBox      SuGtkPalBox;
typedef struct _SuGtkPalBoxClass SuGtkPalBoxClass;

gboolean sugtk_pal_box_append(
    SuGtkPalBox *palbox,
    const suscan_gui_palette_t *pal);

GType sugtk_pal_box_get_type(void);
GtkWidget *sugtk_pal_box_new(void);
const suscan_gui_palette_t *sugtk_pal_box_get_palette(
    const SuGtkPalBox *palbox);
G_END_DECLS

#endif /* _GUI_PALBOX_H */
