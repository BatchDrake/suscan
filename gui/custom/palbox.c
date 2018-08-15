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

#include <glib-object.h>
#include <gdk-pixbuf/gdk-pixbuf-core.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "gradient.h"
#include "palbox.h"

G_DEFINE_TYPE(SuGtkPalBox, sugtk_pal_box, GTK_TYPE_COMBO_BOX);

static GdkPixbuf *
sugtk_pal_box_create_thumbnail(const suscan_gui_pallete_t *pal)
{
  GdkPixbuf *thumbnail = NULL;
  const suscan_gradient_t *gradient = suscan_gui_pallete_get_gradient(pal);
  unsigned int i, j, index;
  unsigned char r, g, b;

  thumbnail = gdk_pixbuf_new_from_data(
      suscan_gui_pallete_get_thumbnail(pal),
      GDK_COLORSPACE_RGB, /* RGB-colorspace */
      FALSE, /* No alpha-channel */
      8, /* Bits per RGB-component */
      SUGTK_PAL_BOX_THUMB_WIDTH,
      SUGTK_PAL_BOX_THUMB_HEIGHT, /* Dimensions */
      3 * SUGTK_PAL_BOX_THUMB_WIDTH, /* Number of bytes between lines (ie stride) */
      NULL,
      NULL); /* Callbacks */

done:
  return thumbnail;
}

gboolean
sugtk_pal_box_append(SuGtkPalBox *palbox, const suscan_gui_pallete_t *pal)
{
  GdkPixbuf *thumbnail;
  GtkTreeIter iter;

  SU_TRYCATCH(
      thumbnail = sugtk_pal_box_create_thumbnail(pal),
      return FALSE);

  gtk_list_store_append(palbox->store, &iter);
  gtk_list_store_set(
      palbox->store,
      &iter,
      0,
      thumbnail,
      1,
      suscan_gui_pallete_get_name(pal),
      2,
      pal,
      -1);

  g_object_unref(G_OBJECT(thumbnail));

  return TRUE;
}

static void
sugtk_pal_box_dispose(GObject* object)
{
  SuGtkPalBox *palbox;

  palbox = SUGTK_PAL_BOX(object);

  if (palbox->store != NULL) {
    g_object_unref(palbox->store);
    palbox->store = NULL;
  }

  if (palbox->def_pal != NULL) {
    suscan_gui_pallete_destroy(palbox->def_pal);
    palbox->def_pal = NULL;
  }

  G_OBJECT_CLASS(sugtk_pal_box_parent_class)->dispose(object);
}

static void
sugtk_pal_box_class_init(SuGtkPalBoxClass *class)
{
  GObjectClass  *g_object_class;

  g_object_class = G_OBJECT_CLASS(class);

  g_object_class->dispose = sugtk_pal_box_dispose;
}

static suscan_gui_pallete_t *
sugtk_pal_box_create_default_pallete(void)
{
  suscan_gui_pallete_t *new = NULL;
  unsigned int i;

  SU_TRYCATCH(new = suscan_gui_pallete_new("Default"), return NULL);

  for (i = 0; i < 256; ++i)
    SU_TRYCATCH(
        suscan_gui_pallete_add_stop(
            new,
            i,
            wf_gradient[i][0],
            wf_gradient[i][1],
            wf_gradient[i][2]),
        return NULL);

  suscan_gui_pallete_compose(new); /* Necessary to create the thumbnail */

  return new;
}

static void
sugtk_pal_box_init(SuGtkPalBox *palbox)
{
  GtkCellRenderer *renderer;

  palbox->store = gtk_list_store_new(
      3,
      GDK_TYPE_PIXBUF,
      G_TYPE_STRING,
      G_TYPE_POINTER);

  gtk_combo_box_set_model(
      &palbox->parent_instance,
      GTK_TREE_MODEL(palbox->store));

  /* icon cell */
  renderer = gtk_cell_renderer_pixbuf_new();
  gtk_cell_layout_pack_start(
      GTK_CELL_LAYOUT(&palbox->parent_instance),
      renderer,
      FALSE);
  gtk_cell_layout_set_attributes(
      GTK_CELL_LAYOUT(&palbox->parent_instance),
      renderer,
      "pixbuf",
      0,
      NULL);

  /* text cell */
  renderer = gtk_cell_renderer_text_new();
  gtk_cell_layout_pack_start(
      GTK_CELL_LAYOUT(&palbox->parent_instance),
      renderer,
      TRUE);
  gtk_cell_layout_set_attributes(
      GTK_CELL_LAYOUT(&palbox->parent_instance),
      renderer,
      "text",
      1,
      NULL);
}

GtkWidget *
sugtk_pal_box_new(void)
{
  GtkWidget *widget = (GtkWidget *) g_object_new(SUGTK_TYPE_PAL_BOX, NULL);
  SuGtkPalBox *palbox = SUGTK_PAL_BOX(widget);
  GtkTreePath *path = NULL;

  SU_TRYCATCH(
      palbox->def_pal = sugtk_pal_box_create_default_pallete(),
      {
          gtk_widget_destroy(widget);
          return NULL;
      });

  (void) sugtk_pal_box_append(palbox, palbox->def_pal);

  gtk_combo_box_set_active(&palbox->parent_instance, 0);

  return widget;
}

