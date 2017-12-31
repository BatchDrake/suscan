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

#define SU_LOG_DOMAIN "mf-ctl"

#include "gui.h"
#include "modemctl.h"

#define SUSCAN_GUI_MODEMCTL_PREFIX "mf."

struct suscan_gui_modemctl_mf {
  GtkBuilder *builder;

  GtkFrame *root;

  GtkComboBoxText *mfTypeComboBoxText;
  GtkScale        *mfRollOffScale;
};

void
suscan_gui_modemctl_mf_destroy(struct suscan_gui_modemctl_mf *mf)
{
  if (mf->builder != NULL)
    g_object_unref(G_OBJECT(mf->builder));

  free(mf);
}

SUPRIVATE void
suscan_gui_modemctl_mf_update_sensitiveness(
    struct suscan_gui_modemctl_mf *mf)
{
  SUBOOL enabled;

  enabled = suscan_gui_modemctl_helper_try_read_combo_id(
      GTK_COMBO_BOX(mf->mfTypeComboBoxText)) == 1;

  gtk_widget_set_sensitive(GTK_WIDGET(mf->mfRollOffScale), enabled);
}

SUPRIVATE SUBOOL
suscan_gui_modemctl_mf_get(
    struct suscan_gui_modemctl_mf *mf,
    suscan_config_t *config)
{
  SU_TRYCATCH(
      suscan_config_set_integer(
          config,
          SUSCAN_GUI_MODEMCTL_PREFIX "type",
          suscan_gui_modemctl_helper_try_read_combo_id(
              GTK_COMBO_BOX(mf->mfTypeComboBoxText))),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_config_set_float(
          config,
          SUSCAN_GUI_MODEMCTL_PREFIX "roll-off",
          gtk_range_get_value(GTK_RANGE(mf->mfRollOffScale))),
      return SU_FALSE);

  suscan_gui_modemctl_mf_update_sensitiveness(mf);

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_gui_modemctl_mf_set(
    struct suscan_gui_modemctl_mf *mf,
    const suscan_config_t *config)
{
  struct suscan_field_value *value;

  gchar id_str[32];

  SU_TRYCATCH(
      value = suscan_config_get_value(
          config,
          SUSCAN_GUI_MODEMCTL_PREFIX "type"),
      return SU_FALSE);

  suscan_gui_modemctl_helper_write_combo_id(
      GTK_COMBO_BOX(mf->mfTypeComboBoxText),
      value->as_int);

  SU_TRYCATCH(
      value = suscan_config_get_value(
          config,
          SUSCAN_GUI_MODEMCTL_PREFIX "roll-off"),
      return SU_FALSE);

  gtk_range_set_value(GTK_RANGE(mf->mfRollOffScale), value->as_float);

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_gui_modemctl_mf_load_all_widgets(struct suscan_gui_modemctl_mf *mf)
{
  SU_TRYCATCH(
      mf->root =
          GTK_FRAME(gtk_builder_get_object(
              mf->builder,
              "fMatchedFilter")),
          return SU_FALSE);

  SU_TRYCATCH(
      mf->mfTypeComboBoxText =
          GTK_COMBO_BOX_TEXT(gtk_builder_get_object(
              mf->builder,
              "cbMFType")),
          return SU_FALSE);

  SU_TRYCATCH(
      mf->mfRollOffScale =
          GTK_SCALE(gtk_builder_get_object(
              mf->builder,
              "sMFRollOff")),
          return SU_FALSE);

  return SU_TRUE;
}

struct suscan_gui_modemctl_mf *
suscan_gui_modemctl_mf_new(const suscan_config_t *config, void *opaque)
{
  struct suscan_gui_modemctl_mf *new = NULL;

  SU_TRYCATCH(
      new = calloc(1, sizeof (struct suscan_gui_modemctl_mf)),
      goto fail);

  SU_TRYCATCH(
      new->builder = gtk_builder_new_from_file(
          PKGDATADIR "/gui/modemctl.glade"),
      goto fail);

  SU_TRYCATCH(suscan_gui_modemctl_mf_load_all_widgets(new), goto fail);

  gtk_builder_connect_signals(new->builder, opaque);

  return new;

fail:
  if (new != NULL)
    suscan_gui_modemctl_mf_destroy(new);

  return NULL;
}

/********************** Modemctl interface boilerplate ***********************/
SUBOOL
suscan_gui_modemctl_mf_applicable(const suscan_config_desc_t *desc)
{
  return suscan_config_desc_has_prefix(desc, SUSCAN_GUI_MODEMCTL_PREFIX);
}

void *
suscan_gui_modemctl_mf_ctor(const suscan_config_t *config, void *opaque)
{
  return suscan_gui_modemctl_mf_new(config, opaque);
}

GtkWidget *
suscan_gui_modemctl_mf_get_root(
    const suscan_gui_modemctl_t *this,
    void *private)
{
  struct suscan_gui_modemctl_mf *mf =
      (struct suscan_gui_modemctl_mf *) private;

  return GTK_WIDGET(mf->root);
}

SUBOOL
suscan_gui_modemctl_mf_get_cb(
    suscan_gui_modemctl_t *this,
    suscan_config_t *config,
    void *private)
{
  struct suscan_gui_modemctl_mf *mf =
      (struct suscan_gui_modemctl_mf *) private;

  return suscan_gui_modemctl_mf_get(mf, config);
}

SUBOOL
suscan_gui_modemctl_mf_set_cb(
    suscan_gui_modemctl_t *this,
    const suscan_config_t *config,
    void *private)
{
  struct suscan_gui_modemctl_mf *mf =
      (struct suscan_gui_modemctl_mf *) private;

  return suscan_gui_modemctl_mf_set(mf, config);
}

void
suscan_gui_modemctl_mf_dtor(void *private)
{
  struct suscan_gui_modemctl_mf *mf =
      (struct suscan_gui_modemctl_mf *) private;

  suscan_gui_modemctl_mf_destroy(mf);
}

/************************** Register AGC class *******************************/
SUBOOL
suscan_gui_modemctl_mf_init(void)
{
  static struct suscan_gui_modemctl_class class = {
      .name       = "mf",
      .applicable = suscan_gui_modemctl_mf_applicable,
      .ctor       = suscan_gui_modemctl_mf_ctor,
      .get_root   = suscan_gui_modemctl_mf_get_root,
      .get        = suscan_gui_modemctl_mf_get_cb,
      .set        = suscan_gui_modemctl_mf_set_cb,
      .dtor       = suscan_gui_modemctl_mf_dtor,
  };

  return suscan_gui_modemctl_class_register(&class);
}
