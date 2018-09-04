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

#define SU_LOG_DOMAIN "ask-ctl"

#include "gui.h"
#include "modemctl.h"

#define SUSCAN_GUI_MODEMCTL_PREFIX "ask."

struct suscan_gui_modemctl_ask {
  GtkBuilder *builder;

  GtkFrame *root;

  GtkSpinButton   *bitsPerLevelSpinButton;
  GtkSpinButton   *askCutoffSpinButton;
  GtkSpinButton   *askOffsetSpinButton;
  GtkCheckButton  *askUsePllCheckButton;
};

void
suscan_gui_modemctl_ask_destroy(struct suscan_gui_modemctl_ask *ask)
{
  if (ask->builder != NULL)
    g_object_unref(G_OBJECT(ask->builder));

  free(ask);
}


SUPRIVATE void
suscan_gui_modemctl_ask_update_sensitiveness(
    struct suscan_gui_modemctl_ask *ask)
{
  gtk_widget_set_sensitive(
      GTK_WIDGET(ask->askCutoffSpinButton),
      gtk_toggle_button_get_active(
          GTK_TOGGLE_BUTTON(ask->askUsePllCheckButton)));
}

SUPRIVATE SUBOOL
suscan_gui_modemctl_ask_get(
    struct suscan_gui_modemctl_ask *ask,
    suscan_config_t *config)
{
  SU_TRYCATCH(
      suscan_config_set_integer(
          config,
          SUSCAN_GUI_MODEMCTL_PREFIX "bits-per-symbol",
          gtk_spin_button_get_value(ask->bitsPerLevelSpinButton)),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_config_set_bool(
          config,
          SUSCAN_GUI_MODEMCTL_PREFIX "use-pll",
          gtk_toggle_button_get_active(
              GTK_TOGGLE_BUTTON(ask->askUsePllCheckButton))),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_config_set_float(
          config,
          SUSCAN_GUI_MODEMCTL_PREFIX "offset",
          gtk_spin_button_get_value(ask->askOffsetSpinButton)),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_config_set_float(
          config,
          SUSCAN_GUI_MODEMCTL_PREFIX "loop-bw",
          gtk_spin_button_get_value(ask->askCutoffSpinButton)),
      return SU_FALSE);

  suscan_gui_modemctl_ask_update_sensitiveness(ask);

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_gui_modemctl_ask_set(
    struct suscan_gui_modemctl_ask *ask,
    const suscan_config_t *config)
{
  struct suscan_field_value *value;

  SU_TRYCATCH(
      value = suscan_config_get_value(
          config,
          SUSCAN_GUI_MODEMCTL_PREFIX "bits-per-symbol"),
      return SU_FALSE);

  gtk_spin_button_set_value(ask->bitsPerLevelSpinButton, value->as_int);

  SU_TRYCATCH(
      value = suscan_config_get_value(
          config,
          SUSCAN_GUI_MODEMCTL_PREFIX "offset"),
      return SU_FALSE);

  gtk_spin_button_set_value(ask->askOffsetSpinButton, value->as_float);

  SU_TRYCATCH(
      value = suscan_config_get_value(
          config,
          SUSCAN_GUI_MODEMCTL_PREFIX "loop-bw"),
      return SU_FALSE);

  gtk_spin_button_set_value(ask->askCutoffSpinButton, value->as_float);

  SU_TRYCATCH(
      value = suscan_config_get_value(
          config,
          SUSCAN_GUI_MODEMCTL_PREFIX "use-pll"),
      return SU_FALSE);

  gtk_toggle_button_set_active(
      GTK_TOGGLE_BUTTON(ask->askUsePllCheckButton),
      value->as_bool);

  suscan_gui_modemctl_ask_update_sensitiveness(ask);

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_gui_modemctl_ask_load_all_widgets(struct suscan_gui_modemctl_ask *ask)
{
  SU_TRYCATCH(
      ask->root =
          GTK_FRAME(gtk_builder_get_object(
              ask->builder,
              "fAskControl")),
          return SU_FALSE);

  SU_TRYCATCH(
      ask->bitsPerLevelSpinButton =
          GTK_SPIN_BUTTON(gtk_builder_get_object(
              ask->builder,
              "sbBitsPerLevel")),
          return SU_FALSE);

  SU_TRYCATCH(
      ask->askCutoffSpinButton =
          GTK_SPIN_BUTTON(gtk_builder_get_object(
              ask->builder,
              "sbAskCutoff")),
          return SU_FALSE);

  SU_TRYCATCH(
      ask->askOffsetSpinButton =
          GTK_SPIN_BUTTON(gtk_builder_get_object(
              ask->builder,
              "sbAskOffset")),
          return SU_FALSE);

  SU_TRYCATCH(
      ask->askUsePllCheckButton =
          GTK_CHECK_BUTTON(gtk_builder_get_object(
              ask->builder,
              "cbAskUsePll")),
          return SU_FALSE);

  return SU_TRUE;
}

struct suscan_gui_modemctl_ask *
suscan_gui_modemctl_ask_new(const suscan_config_t *config, void *opaque)
{
  struct suscan_gui_modemctl_ask *new = NULL;

  SU_TRYCATCH(
      new = calloc(1, sizeof (struct suscan_gui_modemctl_ask)),
      goto fail);

  SU_TRYCATCH(
      new->builder = gtk_builder_new_from_file(
          PKGDATADIR "/gui/modemctl.glade"),
      goto fail);

  SU_TRYCATCH(suscan_gui_modemctl_ask_load_all_widgets(new), goto fail);

  gtk_builder_connect_signals(new->builder, opaque);

  return new;

fail:
  if (new != NULL)
    suscan_gui_modemctl_ask_destroy(new);

  return NULL;
}

/********************** Modemctl interface boilerplate ***********************/
SUBOOL
suscan_gui_modemctl_ask_applicable(const suscan_config_desc_t *desc)
{
  return suscan_config_desc_has_prefix(desc, SUSCAN_GUI_MODEMCTL_PREFIX);
}

void *
suscan_gui_modemctl_ask_ctor(const suscan_config_t *config, void *opaque)
{
  return suscan_gui_modemctl_ask_new(config, opaque);
}

GtkWidget *
suscan_gui_modemctl_ask_get_root(
    const suscan_gui_modemctl_t *this,
    void *private)
{
  struct suscan_gui_modemctl_ask *ask =
      (struct suscan_gui_modemctl_ask *) private;

  return GTK_WIDGET(ask->root);
}

SUBOOL
suscan_gui_modemctl_ask_get_cb(
    suscan_gui_modemctl_t *this,
    suscan_config_t *config,
    void *private)
{
  struct suscan_gui_modemctl_ask *ask =
      (struct suscan_gui_modemctl_ask *) private;

  return suscan_gui_modemctl_ask_get(ask, config);
}

SUBOOL
suscan_gui_modemctl_ask_set_cb(
    suscan_gui_modemctl_t *this,
    const suscan_config_t *config,
    void *private)
{
  struct suscan_gui_modemctl_ask *ask =
      (struct suscan_gui_modemctl_ask *) private;

  return suscan_gui_modemctl_ask_set(ask, config);
}

void
suscan_gui_modemctl_ask_dtor(void *private)
{
  struct suscan_gui_modemctl_ask *ask =
      (struct suscan_gui_modemctl_ask *) private;

  suscan_gui_modemctl_ask_destroy(ask);
}

/************************** Register AGC class *******************************/
SUBOOL
suscan_gui_modemctl_ask_init(void)
{
  static struct suscan_gui_modemctl_class class = {
      .name       = "ask",
      .applicable = suscan_gui_modemctl_ask_applicable,
      .ctor       = suscan_gui_modemctl_ask_ctor,
      .get_root   = suscan_gui_modemctl_ask_get_root,
      .get        = suscan_gui_modemctl_ask_get_cb,
      .set        = suscan_gui_modemctl_ask_set_cb,
      .dtor       = suscan_gui_modemctl_ask_dtor,
  };

  return suscan_gui_modemctl_class_register(&class);
}
