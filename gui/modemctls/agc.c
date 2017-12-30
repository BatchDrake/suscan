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

#define SU_LOG_DOMAIN "agc-ctl"

#include "gui.h"
#include "modemctl.h"

#define SUSCAN_GUI_MODEMCTL_PREFIX "agc."

struct suscan_gui_modemctl_agc {
  GtkBuilder *builder;

  GtkFrame *root;
  GtkEntry *agcGainEntry;
  GtkToggleButton *agcAutoToggleButton;

  SUFLOAT gain;
};

void
suscan_gui_modemctl_agc_destroy(struct suscan_gui_modemctl_agc *agc)
{
  if (agc->builder != NULL)
    g_object_unref(G_OBJECT(agc->builder));

  free(agc);
}

SUPRIVATE SUBOOL
suscan_gui_modemctl_agc_get(
    struct suscan_gui_modemctl_agc *agc,
    suscan_config_t *config)
{
  suscan_gui_modemctl_helper_try_read_float(
      agc->agcGainEntry,
      &agc->gain);

  SU_TRYCATCH(
      suscan_config_set_float(
          config,
          SUSCAN_GUI_MODEMCTL_PREFIX "gain",
          agc->gain),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_config_set_bool(
          config,
          SUSCAN_GUI_MODEMCTL_PREFIX "enabled",
          gtk_toggle_button_get_active(agc->agcAutoToggleButton)),
      return SU_FALSE);

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_gui_modemctl_agc_set(
    struct suscan_gui_modemctl_agc *agc,
    const suscan_config_t *config)
{
  struct suscan_field_value *value;

  SU_TRYCATCH(
      value = suscan_config_get_value(
          config,
          SUSCAN_GUI_MODEMCTL_PREFIX "gain"),
      return SU_FALSE);

  agc->gain = value->as_float;

  SU_TRYCATCH(
      value = suscan_config_get_value(
          config,
          SUSCAN_GUI_MODEMCTL_PREFIX "enabled"),
      return SU_FALSE);

  gtk_toggle_button_set_active(agc->agcAutoToggleButton, value->as_bool);

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_gui_modemctl_agc_load_all_widgets(struct suscan_gui_modemctl_agc *agc)
{
  SU_TRYCATCH(
      agc->root =
          GTK_FRAME(gtk_builder_get_object(
              agc->builder,
              "fGainControl")),
          return SU_FALSE);

  SU_TRYCATCH(
      agc->agcGainEntry =
          GTK_ENTRY(gtk_builder_get_object(
              agc->builder,
              "eAGCGain")),
          return SU_FALSE);

  SU_TRYCATCH(
      agc->agcAutoToggleButton =
          GTK_TOGGLE_BUTTON(gtk_builder_get_object(
              agc->builder,
              "tbAGCAuto")),
          return SU_FALSE);

  return SU_TRUE;
}

struct suscan_gui_modemctl_agc *
suscan_gui_modemctl_agc_new(const suscan_config_t *config)
{
  struct suscan_gui_modemctl_agc *new = NULL;

  SU_TRYCATCH(
      new = calloc(1, sizeof (struct suscan_gui_modemctl_agc)),
      goto fail);

  SU_TRYCATCH(
      new->builder = gtk_builder_new_from_file(
          PKGDATADIR "/gui/modemctl.glade"),
      goto fail);

  SU_TRYCATCH(suscan_gui_modemctl_agc_load_all_widgets(new), goto fail);

  return new;

fail:
  if (new != NULL)
    suscan_gui_modemctl_agc_destroy(new);

  return NULL;
}

/********************** Modemctl interface boilerplate ***********************/
SUBOOL
suscan_gui_modemctl_agc_applicable(const suscan_config_desc_t *desc)
{
  return suscan_config_desc_has_prefix(desc, SUSCAN_GUI_MODEMCTL_PREFIX);
}

void *
suscan_gui_modemctl_agc_ctor(const suscan_config_t *config)
{
  return suscan_gui_modemctl_agc_new(config);
}

GtkWidget *
suscan_gui_modemctl_agc_get_root(
    const suscan_gui_modemctl_t *this,
    void *private)
{
  struct suscan_gui_modemctl_agc *agc =
      (struct suscan_gui_modemctl_agc *) private;

  return GTK_WIDGET(agc->root);
}

SUBOOL
suscan_gui_modemctl_agc_get_cb(
    suscan_gui_modemctl_t *this,
    suscan_config_t *config,
    void *private)
{
  struct suscan_gui_modemctl_agc *agc =
      (struct suscan_gui_modemctl_agc *) private;

  return suscan_gui_modemctl_agc_get(agc, config);
}

SUBOOL
suscan_gui_modemctl_agc_set_cb(
    suscan_gui_modemctl_t *this,
    const suscan_config_t *config,
    void *private)
{
  struct suscan_gui_modemctl_agc *agc =
      (struct suscan_gui_modemctl_agc *) private;

  return suscan_gui_modemctl_agc_set(agc, config);
}

void
suscan_gui_modemctl_agc_dtor(void *private)
{
  struct suscan_gui_modemctl_agc *agc =
      (struct suscan_gui_modemctl_agc *) private;

  suscan_gui_modemctl_agc_destroy(agc);
}

/************************** Register AGC class *******************************/
SUBOOL
suscan_gui_modemctl_agc_init(void)
{
  static struct suscan_gui_modemctl_class class = {
      .name       = "agc",
      .applicable = suscan_gui_modemctl_agc_applicable,
      .ctor       = suscan_gui_modemctl_agc_ctor,
      .get_root   = suscan_gui_modemctl_agc_get_root,
      .get        = suscan_gui_modemctl_agc_get_cb,
      .set        = suscan_gui_modemctl_agc_set_cb,
      .dtor       = suscan_gui_modemctl_agc_dtor,
  };

  return suscan_gui_modemctl_class_register(&class);
}
