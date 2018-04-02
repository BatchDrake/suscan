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

#define SU_LOG_DOMAIN "fsk-ctl"

#include "gui.h"
#include "modemctl.h"

#define SUSCAN_GUI_MODEMCTL_PREFIX "fsk."

struct suscan_gui_modemctl_fsk {
  GtkBuilder *builder;

  GtkFrame *root;

  GtkSpinButton   *bitsPerToneSpinButton;
};

void
suscan_gui_modemctl_fsk_destroy(struct suscan_gui_modemctl_fsk *fsk)
{
  if (fsk->builder != NULL)
    g_object_unref(G_OBJECT(fsk->builder));

  free(fsk);
}


SUPRIVATE SUBOOL
suscan_gui_modemctl_fsk_get(
    struct suscan_gui_modemctl_fsk *fsk,
    suscan_config_t *config)
{
  SU_TRYCATCH(
      suscan_config_set_integer(
          config,
          SUSCAN_GUI_MODEMCTL_PREFIX "bits-per-symbol",
          gtk_spin_button_get_value(fsk->bitsPerToneSpinButton)),
      return SU_FALSE);

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_gui_modemctl_fsk_set(
    struct suscan_gui_modemctl_fsk *fsk,
    const suscan_config_t *config)
{
  struct suscan_field_value *value;

  SU_TRYCATCH(
      value = suscan_config_get_value(
          config,
          SUSCAN_GUI_MODEMCTL_PREFIX "bits-per-symbol"),
      return SU_FALSE);

  gtk_spin_button_set_value(fsk->bitsPerToneSpinButton, value->as_int);

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_gui_modemctl_fsk_load_all_widgets(struct suscan_gui_modemctl_fsk *fsk)
{
  SU_TRYCATCH(
      fsk->root =
          GTK_FRAME(gtk_builder_get_object(
              fsk->builder,
              "fFskControl")),
          return SU_FALSE);


  SU_TRYCATCH(
      fsk->bitsPerToneSpinButton =
          GTK_SPIN_BUTTON(gtk_builder_get_object(
              fsk->builder,
              "sbBitsPerTone")),
          return SU_FALSE);

  return SU_TRUE;
}

struct suscan_gui_modemctl_fsk *
suscan_gui_modemctl_fsk_new(const suscan_config_t *config, void *opaque)
{
  struct suscan_gui_modemctl_fsk *new = NULL;

  SU_TRYCATCH(
      new = calloc(1, sizeof (struct suscan_gui_modemctl_fsk)),
      goto fail);

  SU_TRYCATCH(
      new->builder = gtk_builder_new_from_file(
          PKGDATADIR "/gui/modemctl.glade"),
      goto fail);

  SU_TRYCATCH(suscan_gui_modemctl_fsk_load_all_widgets(new), goto fail);

  gtk_builder_connect_signals(new->builder, opaque);

  return new;

fail:
  if (new != NULL)
    suscan_gui_modemctl_fsk_destroy(new);

  return NULL;
}

/********************** Modemctl interface boilerplate ***********************/
SUBOOL
suscan_gui_modemctl_fsk_applicable(const suscan_config_desc_t *desc)
{
  return suscan_config_desc_has_prefix(desc, SUSCAN_GUI_MODEMCTL_PREFIX);
}

void *
suscan_gui_modemctl_fsk_ctor(const suscan_config_t *config, void *opaque)
{
  return suscan_gui_modemctl_fsk_new(config, opaque);
}

GtkWidget *
suscan_gui_modemctl_fsk_get_root(
    const suscan_gui_modemctl_t *this,
    void *private)
{
  struct suscan_gui_modemctl_fsk *fsk =
      (struct suscan_gui_modemctl_fsk *) private;

  return GTK_WIDGET(fsk->root);
}

SUBOOL
suscan_gui_modemctl_fsk_get_cb(
    suscan_gui_modemctl_t *this,
    suscan_config_t *config,
    void *private)
{
  struct suscan_gui_modemctl_fsk *fsk =
      (struct suscan_gui_modemctl_fsk *) private;

  return suscan_gui_modemctl_fsk_get(fsk, config);
}

SUBOOL
suscan_gui_modemctl_fsk_set_cb(
    suscan_gui_modemctl_t *this,
    const suscan_config_t *config,
    void *private)
{
  struct suscan_gui_modemctl_fsk *fsk =
      (struct suscan_gui_modemctl_fsk *) private;

  return suscan_gui_modemctl_fsk_set(fsk, config);
}

void
suscan_gui_modemctl_fsk_dtor(void *private)
{
  struct suscan_gui_modemctl_fsk *fsk =
      (struct suscan_gui_modemctl_fsk *) private;

  suscan_gui_modemctl_fsk_destroy(fsk);
}

/************************** Register AGC class *******************************/
SUBOOL
suscan_gui_modemctl_fsk_init(void)
{
  static struct suscan_gui_modemctl_class class = {
      .name       = "fsk",
      .applicable = suscan_gui_modemctl_fsk_applicable,
      .ctor       = suscan_gui_modemctl_fsk_ctor,
      .get_root   = suscan_gui_modemctl_fsk_get_root,
      .get        = suscan_gui_modemctl_fsk_get_cb,
      .set        = suscan_gui_modemctl_fsk_set_cb,
      .dtor       = suscan_gui_modemctl_fsk_dtor,
  };

  return suscan_gui_modemctl_class_register(&class);
}
