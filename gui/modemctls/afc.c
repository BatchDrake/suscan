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

#define SU_LOG_DOMAIN "afc-ctl"

#include "gui.h"
#include "modemctl.h"

#define SUSCAN_GUI_MODEMCTL_PREFIX "afc."

struct suscan_gui_modemctl_afc {
  GtkBuilder *builder;

  GtkFrame *root;

  GtkComboBoxText *fcTypeComboBoxText;
  GtkComboBoxText *fcOrderComboBoxText;
  GtkSpinButton   *fcOffsetSpinButton;
};

void
suscan_gui_modemctl_afc_destroy(struct suscan_gui_modemctl_afc *afc)
{
  if (afc->builder != NULL)
    g_object_unref(G_OBJECT(afc->builder));

  free(afc);
}

SUPRIVATE void
suscan_gui_modemctl_afc_update_sensitiveness(
    struct suscan_gui_modemctl_afc *afc)
{
  SUBOOL manual;

  manual = suscan_gui_modemctl_helper_try_read_combo_id(
      GTK_COMBO_BOX(afc->fcTypeComboBoxText)) == 0;

  gtk_widget_set_sensitive(GTK_WIDGET(afc->fcOrderComboBoxText), manual);
  gtk_widget_set_sensitive(GTK_WIDGET(afc->fcOffsetSpinButton), manual);
}

SUPRIVATE SUBOOL
suscan_gui_modemctl_afc_get(
    struct suscan_gui_modemctl_afc *afc,
    suscan_config_t *config)
{
  SU_TRYCATCH(
      suscan_config_set_integer(
          config,
          SUSCAN_GUI_MODEMCTL_PREFIX "costas-order",
          suscan_gui_modemctl_helper_try_read_combo_id(
              GTK_COMBO_BOX(afc->fcTypeComboBoxText))),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_config_set_integer(
          config,
          SUSCAN_GUI_MODEMCTL_PREFIX "bits-per-symbol",
          suscan_gui_modemctl_helper_try_read_combo_id(
              GTK_COMBO_BOX(afc->fcOrderComboBoxText))),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_config_set_float(
          config,
          SUSCAN_GUI_MODEMCTL_PREFIX "offset",
          gtk_spin_button_get_value(afc->fcOffsetSpinButton)),
      return SU_FALSE);

  suscan_gui_modemctl_afc_update_sensitiveness(afc);

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_gui_modemctl_afc_set(
    struct suscan_gui_modemctl_afc *afc,
    const suscan_config_t *config)
{
  struct suscan_field_value *value;

  gchar id_str[32];

  SU_TRYCATCH(
      value = suscan_config_get_value(
          config,
          SUSCAN_GUI_MODEMCTL_PREFIX "costas-order"),
      return SU_FALSE);

  suscan_gui_modemctl_helper_write_combo_id(
      GTK_COMBO_BOX(afc->fcTypeComboBoxText),
      value->as_int);

  SU_TRYCATCH(
      value = suscan_config_get_value(
          config,
          SUSCAN_GUI_MODEMCTL_PREFIX "bits-per-symbol"),
      return SU_FALSE);

  suscan_gui_modemctl_helper_write_combo_id(
      GTK_COMBO_BOX(afc->fcOrderComboBoxText),
      value->as_int);

  SU_TRYCATCH(
      value = suscan_config_get_value(
          config,
          SUSCAN_GUI_MODEMCTL_PREFIX "offset"),
      return SU_FALSE);

  gtk_spin_button_set_value(afc->fcOffsetSpinButton, value->as_float);

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_gui_modemctl_afc_load_all_widgets(struct suscan_gui_modemctl_afc *afc)
{
  SU_TRYCATCH(
      afc->root =
          GTK_FRAME(gtk_builder_get_object(
              afc->builder,
              "fCarrierRecovery")),
          return SU_FALSE);

  SU_TRYCATCH(
      afc->fcTypeComboBoxText =
          GTK_COMBO_BOX_TEXT(gtk_builder_get_object(
              afc->builder,
              "cbFCType")),
          return SU_FALSE);

  SU_TRYCATCH(
      afc->fcOrderComboBoxText =
          GTK_COMBO_BOX_TEXT(gtk_builder_get_object(
              afc->builder,
              "cbFCOrder")),
          return SU_FALSE);

  SU_TRYCATCH(
      afc->fcOffsetSpinButton =
          GTK_SPIN_BUTTON(gtk_builder_get_object(
              afc->builder,
              "sbFCOffset")),
          return SU_FALSE);

  return SU_TRUE;
}

struct suscan_gui_modemctl_afc *
suscan_gui_modemctl_afc_new(const suscan_config_t *config, void *opaque)
{
  struct suscan_gui_modemctl_afc *new = NULL;

  SU_TRYCATCH(
      new = calloc(1, sizeof (struct suscan_gui_modemctl_afc)),
      goto fail);

  SU_TRYCATCH(
      new->builder = gtk_builder_new_from_file(
          PKGDATADIR "/gui/modemctl.glade"),
      goto fail);

  SU_TRYCATCH(suscan_gui_modemctl_afc_load_all_widgets(new), goto fail);

  gtk_builder_connect_signals(new->builder, opaque);

  return new;

fail:
  if (new != NULL)
    suscan_gui_modemctl_afc_destroy(new);

  return NULL;
}

/********************** Modemctl interface boilerplate ***********************/
SUBOOL
suscan_gui_modemctl_afc_applicable(const suscan_config_desc_t *desc)
{
  return suscan_config_desc_has_prefix(desc, SUSCAN_GUI_MODEMCTL_PREFIX);
}

void *
suscan_gui_modemctl_afc_ctor(const suscan_config_t *config, void *opaque)
{
  return suscan_gui_modemctl_afc_new(config, opaque);
}

GtkWidget *
suscan_gui_modemctl_afc_get_root(
    const suscan_gui_modemctl_t *this,
    void *private)
{
  struct suscan_gui_modemctl_afc *afc =
      (struct suscan_gui_modemctl_afc *) private;

  return GTK_WIDGET(afc->root);
}

SUBOOL
suscan_gui_modemctl_afc_get_cb(
    suscan_gui_modemctl_t *this,
    suscan_config_t *config,
    void *private)
{
  struct suscan_gui_modemctl_afc *afc =
      (struct suscan_gui_modemctl_afc *) private;

  return suscan_gui_modemctl_afc_get(afc, config);
}

SUBOOL
suscan_gui_modemctl_afc_set_cb(
    suscan_gui_modemctl_t *this,
    const suscan_config_t *config,
    void *private)
{
  struct suscan_gui_modemctl_afc *afc =
      (struct suscan_gui_modemctl_afc *) private;

  return suscan_gui_modemctl_afc_set(afc, config);
}

void
suscan_gui_modemctl_afc_dtor(void *private)
{
  struct suscan_gui_modemctl_afc *afc =
      (struct suscan_gui_modemctl_afc *) private;

  suscan_gui_modemctl_afc_destroy(afc);
}

/************************** Register AGC class *******************************/
SUBOOL
suscan_gui_modemctl_afc_init(void)
{
  static struct suscan_gui_modemctl_class class = {
      .name       = "afc",
      .applicable = suscan_gui_modemctl_afc_applicable,
      .ctor       = suscan_gui_modemctl_afc_ctor,
      .get_root   = suscan_gui_modemctl_afc_get_root,
      .get        = suscan_gui_modemctl_afc_get_cb,
      .set        = suscan_gui_modemctl_afc_set_cb,
      .dtor       = suscan_gui_modemctl_afc_dtor,
  };

  return suscan_gui_modemctl_class_register(&class);
}
