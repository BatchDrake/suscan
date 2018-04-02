/*

  Copyright (C) 2017 Gonzalo José Carracedo Carballal

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Pfublic License as
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

#define SU_LOG_DOMAIN "equalizer-ctl"

#include "gui.h"
#include "modemctl.h"

#define SUSCAN_GUI_MODEMCTL_PREFIX "equalizer."

struct suscan_gui_modemctl_equalizer {
  GtkBuilder *builder;

  GtkFrame *root;

  GtkComboBoxText *eqTypeComboBoxText;
  GtkEntry        *eqRateEntry;
  GtkToggleButton *eqLockToggleButton;
  GtkButton       *eqResetButton;

  SUFLOAT         rate;
};

void
suscan_gui_modemctl_equalizer_destroy(
    struct suscan_gui_modemctl_equalizer *equalizer)
{
  if (equalizer->builder != NULL)
    g_object_unref(G_OBJECT(equalizer->builder));

  free(equalizer);
}

SUPRIVATE void
suscan_gui_modemctl_equalizer_update_sensitiveness(
    struct suscan_gui_modemctl_equalizer *equalizer)
{
  SUBOOL enabled;

  enabled = suscan_gui_modemctl_helper_try_read_combo_id(
      GTK_COMBO_BOX(equalizer->eqTypeComboBoxText)) == 1;

  gtk_widget_set_sensitive(GTK_WIDGET(equalizer->eqLockToggleButton), enabled);
  gtk_widget_set_sensitive(GTK_WIDGET(equalizer->eqResetButton), enabled);

  gtk_widget_set_sensitive(
      GTK_WIDGET(equalizer->eqRateEntry),
      enabled && !gtk_toggle_button_get_active(equalizer->eqLockToggleButton));
}

SUPRIVATE SUBOOL
suscan_gui_modemctl_equalizer_get(
    struct suscan_gui_modemctl_equalizer *equalizer,
    suscan_config_t *config)
{
  SU_TRYCATCH(
      suscan_config_set_integer(
          config,
          SUSCAN_GUI_MODEMCTL_PREFIX "type",
          suscan_gui_modemctl_helper_try_read_combo_id(
              GTK_COMBO_BOX(equalizer->eqTypeComboBoxText))),
      return SU_FALSE);

  suscan_gui_modemctl_helper_try_read_float(
      equalizer->eqRateEntry,
      &equalizer->rate);

  SU_TRYCATCH(
      suscan_config_set_float(
          config,
          SUSCAN_GUI_MODEMCTL_PREFIX "rate",
          equalizer->rate),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_config_set_bool(
          config,
          SUSCAN_GUI_MODEMCTL_PREFIX "locked",
          gtk_toggle_button_get_active(equalizer->eqLockToggleButton)),
      return SU_FALSE);

  suscan_gui_modemctl_equalizer_update_sensitiveness(equalizer);

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_gui_modemctl_equalizer_set(
    struct suscan_gui_modemctl_equalizer *equalizer,
    const suscan_config_t *config)
{
  struct suscan_field_value *value;

  SU_TRYCATCH(
      value = suscan_config_get_value(
          config,
          SUSCAN_GUI_MODEMCTL_PREFIX "type"),
      return SU_FALSE);

  suscan_gui_modemctl_helper_write_combo_id(
      GTK_COMBO_BOX(equalizer->eqTypeComboBoxText),
      value->as_int);

  SU_TRYCATCH(
      value = suscan_config_get_value(
          config,
          SUSCAN_GUI_MODEMCTL_PREFIX "rate"),
      return SU_FALSE);

  suscan_gui_modemctl_helper_write_float(
      equalizer->eqRateEntry,
      value->as_float);

  SU_TRYCATCH(
      value = suscan_config_get_value(
          config,
          SUSCAN_GUI_MODEMCTL_PREFIX "locked"),
      return SU_FALSE);

  gtk_toggle_button_set_active(equalizer->eqLockToggleButton, value->as_bool);

  suscan_gui_modemctl_equalizer_update_sensitiveness(equalizer);

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_gui_modemctl_equalizer_load_all_widgets(
    struct suscan_gui_modemctl_equalizer *equalizer)
{
  SU_TRYCATCH(
      equalizer->root =
          GTK_FRAME(gtk_builder_get_object(
              equalizer->builder,
              "fEqualization")),
          return SU_FALSE);

  SU_TRYCATCH(
      equalizer->eqTypeComboBoxText =
          GTK_COMBO_BOX_TEXT(gtk_builder_get_object(
              equalizer->builder,
              "cbEQType")),
          return SU_FALSE);

  SU_TRYCATCH(
      equalizer->eqRateEntry =
          GTK_ENTRY(gtk_builder_get_object(
              equalizer->builder,
              "eEQRate")),
          return SU_FALSE);

  SU_TRYCATCH(
      equalizer->eqLockToggleButton =
          GTK_TOGGLE_BUTTON(gtk_builder_get_object(
              equalizer->builder,
              "tbEQLock")),
          return SU_FALSE);

  SU_TRYCATCH(
      equalizer->eqResetButton =
          GTK_BUTTON(gtk_builder_get_object(
              equalizer->builder,
              "bEQReset")),
          return SU_FALSE);

  return SU_TRUE;
}

struct suscan_gui_modemctl_equalizer *
suscan_gui_modemctl_equalizer_new(const suscan_config_t *config, void *opaque)
{
  struct suscan_gui_modemctl_equalizer *new = NULL;

  SU_TRYCATCH(
      new = calloc(1, sizeof (struct suscan_gui_modemctl_equalizer)),
      goto fail);

  SU_TRYCATCH(
      new->builder = gtk_builder_new_from_file(
          PKGDATADIR "/gui/modemctl.glade"),
      goto fail);

  SU_TRYCATCH(suscan_gui_modemctl_equalizer_load_all_widgets(new), goto fail);

  gtk_builder_connect_signals(new->builder, opaque);

  return new;

fail:
  if (new != NULL)
    suscan_gui_modemctl_equalizer_destroy(new);

  return NULL;
}

/********************** Modemctl interface boilerplate ***********************/
SUBOOL
suscan_gui_modemctl_equalizer_applicable(const suscan_config_desc_t *desc)
{
  return suscan_config_desc_has_prefix(desc, SUSCAN_GUI_MODEMCTL_PREFIX);
}

void *
suscan_gui_modemctl_equalizer_ctor(const suscan_config_t *config, void *opaque)
{
  return suscan_gui_modemctl_equalizer_new(config, opaque);
}

GtkWidget *
suscan_gui_modemctl_equalizer_get_root(
    const suscan_gui_modemctl_t *this,
    void *private)
{
  struct suscan_gui_modemctl_equalizer *equalizer =
      (struct suscan_gui_modemctl_equalizer *) private;

  return GTK_WIDGET(equalizer->root);
}

SUBOOL
suscan_gui_modemctl_equalizer_get_cb(
    suscan_gui_modemctl_t *this,
    suscan_config_t *config,
    void *private)
{
  struct suscan_gui_modemctl_equalizer *equalizer =
      (struct suscan_gui_modemctl_equalizer *) private;

  return suscan_gui_modemctl_equalizer_get(equalizer, config);
}

SUBOOL
suscan_gui_modemctl_equalizer_set_cb(
    suscan_gui_modemctl_t *this,
    const suscan_config_t *config,
    void *private)
{
  struct suscan_gui_modemctl_equalizer *equalizer =
      (struct suscan_gui_modemctl_equalizer *) private;

  return suscan_gui_modemctl_equalizer_set(equalizer, config);
}

void
suscan_gui_modemctl_equalizer_dtor(void *private)
{
  struct suscan_gui_modemctl_equalizer *equalizer =
      (struct suscan_gui_modemctl_equalizer *) private;

  suscan_gui_modemctl_equalizer_destroy(equalizer);
}

/************************** Register AGC class *******************************/
SUBOOL
suscan_gui_modemctl_equalizer_init(void)
{
  static struct suscan_gui_modemctl_class class = {
      .name       = "equalizer",
      .applicable = suscan_gui_modemctl_equalizer_applicable,
      .ctor       = suscan_gui_modemctl_equalizer_ctor,
      .get_root   = suscan_gui_modemctl_equalizer_get_root,
      .get        = suscan_gui_modemctl_equalizer_get_cb,
      .set        = suscan_gui_modemctl_equalizer_set_cb,
      .dtor       = suscan_gui_modemctl_equalizer_dtor,
  };

  return suscan_gui_modemctl_class_register(&class);
}
