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

#define SU_LOG_DOMAIN "clock-ctl"

#include "gui.h"
#include "modemctl.h"

#define SUSCAN_GUI_MODEMCTL_PREFIX "clock."

struct suscan_gui_modemctl_clock {
  GtkBuilder *builder;

  GtkFrame *root;

  GtkEntry        *crBaudEntry;
  GtkToggleButton *crRunningToggleButton;
  GtkComboBoxText *crTypeComboBoxText;
  GtkEntry        *crGainEntry;
  GtkScale        *crPhaseScale;

  SUFLOAT         gain;
  SUFLOAT         baud;
};

void
suscan_gui_modemctl_clock_destroy(struct suscan_gui_modemctl_clock *clock)
{
  if (clock->builder != NULL)
    g_object_unref(G_OBJECT(clock->builder));

  free(clock);
}

SUPRIVATE void
suscan_gui_modemctl_clock_update_sensitiveness(
    struct suscan_gui_modemctl_clock *clock)
{
  SUBOOL manual;
  GtkLabel *label;

  manual = suscan_gui_modemctl_helper_try_read_combo_id(
      GTK_COMBO_BOX(clock->crTypeComboBoxText)) == 0;

  gtk_widget_set_sensitive(GTK_WIDGET(clock->crPhaseScale), manual);
  gtk_widget_set_sensitive(GTK_WIDGET(clock->crGainEntry), !manual);

  gtk_button_set_label(
      GTK_BUTTON(clock->crRunningToggleButton),
      gtk_toggle_button_get_active(clock->crRunningToggleButton)
        ? "Stop"
        : "Start");
}

SUPRIVATE SUBOOL
suscan_gui_modemctl_clock_get(
    struct suscan_gui_modemctl_clock *clock,
    suscan_config_t *config)
{
  SU_TRYCATCH(
      suscan_config_set_integer(
          config,
          SUSCAN_GUI_MODEMCTL_PREFIX "type",
          suscan_gui_modemctl_helper_try_read_combo_id(
              GTK_COMBO_BOX(clock->crTypeComboBoxText))),
      return SU_FALSE);

  suscan_gui_modemctl_helper_try_read_float(clock->crBaudEntry, &clock->baud);

  SU_TRYCATCH(
      suscan_config_set_float(
          config,
          SUSCAN_GUI_MODEMCTL_PREFIX "baud",
          clock->baud),
      return SU_FALSE);

  suscan_gui_modemctl_helper_try_read_float(clock->crGainEntry, &clock->gain);

  SU_TRYCATCH(
      suscan_config_set_float(
          config,
          SUSCAN_GUI_MODEMCTL_PREFIX "gain",
          clock->gain),
      return SU_FALSE);


  SU_TRYCATCH(
      suscan_config_set_float(
          config,
          SUSCAN_GUI_MODEMCTL_PREFIX "phase",
          gtk_range_get_value(GTK_RANGE(clock->crPhaseScale))),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_config_set_bool(
          config,
          SUSCAN_GUI_MODEMCTL_PREFIX "running",
          gtk_toggle_button_get_active(clock->crRunningToggleButton)),
      return SU_FALSE);

  suscan_gui_modemctl_clock_update_sensitiveness(clock);

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_gui_modemctl_clock_set(
    struct suscan_gui_modemctl_clock *clock,
    const suscan_config_t *config)
{
  struct suscan_field_value *value;

  SU_TRYCATCH(
      value = suscan_config_get_value(
          config,
          SUSCAN_GUI_MODEMCTL_PREFIX "type"),
      return SU_FALSE);

  suscan_gui_modemctl_helper_write_combo_id(
      GTK_COMBO_BOX(clock->crTypeComboBoxText),
      value->as_int);

  SU_TRYCATCH(
      value = suscan_config_get_value(
          config,
          SUSCAN_GUI_MODEMCTL_PREFIX "baud"),
      return SU_FALSE);

  suscan_gui_modemctl_helper_write_float(clock->crBaudEntry, value->as_float);

  clock->baud = value->as_float;

  SU_TRYCATCH(
      value = suscan_config_get_value(
          config,
          SUSCAN_GUI_MODEMCTL_PREFIX "gain"),
      return SU_FALSE);

  suscan_gui_modemctl_helper_write_float(clock->crGainEntry, value->as_float);

  clock->gain = value->as_float;

  SU_TRYCATCH(
      value = suscan_config_get_value(
          config,
          SUSCAN_GUI_MODEMCTL_PREFIX "phase"),
      return SU_FALSE);

  gtk_range_set_value(GTK_RANGE(clock->crPhaseScale), value->as_float);

  SU_TRYCATCH(
      value = suscan_config_get_value(
          config,
          SUSCAN_GUI_MODEMCTL_PREFIX "running"),
      return SU_FALSE);

  gtk_toggle_button_set_active(clock->crRunningToggleButton, value->as_bool);

  suscan_gui_modemctl_clock_update_sensitiveness(clock);

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_gui_modemctl_clock_load_all_widgets(
    struct suscan_gui_modemctl_clock *clock)
{
  SU_TRYCATCH(
      clock->root =
          GTK_FRAME(gtk_builder_get_object(
              clock->builder,
              "fClockRecovery")),
          return SU_FALSE);

  SU_TRYCATCH(
      clock->crTypeComboBoxText =
          GTK_COMBO_BOX_TEXT(gtk_builder_get_object(
              clock->builder,
              "cbCRType")),
          return SU_FALSE);

  SU_TRYCATCH(
      clock->crBaudEntry =
          GTK_ENTRY(gtk_builder_get_object(
              clock->builder,
              "eCRBaud")),
          return SU_FALSE);

  SU_TRYCATCH(
      clock->crGainEntry =
          GTK_ENTRY(gtk_builder_get_object(
              clock->builder,
              "eCRGain")),
          return SU_FALSE);

  SU_TRYCATCH(
      clock->crPhaseScale =
          GTK_SCALE(gtk_builder_get_object(
              clock->builder,
              "sCRPhase")),
          return SU_FALSE);

  SU_TRYCATCH(
      clock->crRunningToggleButton =
          GTK_TOGGLE_BUTTON(gtk_builder_get_object(
              clock->builder,
              "tbCRRunning")),
          return SU_FALSE);

  return SU_TRUE;
}

struct suscan_gui_modemctl_clock *
suscan_gui_modemctl_clock_new(const suscan_config_t *config, void *opaque)
{
  struct suscan_gui_modemctl_clock *new = NULL;

  SU_TRYCATCH(
      new = calloc(1, sizeof (struct suscan_gui_modemctl_clock)),
      goto fail);

  SU_TRYCATCH(
      new->builder = gtk_builder_new_from_file(
          PKGDATADIR "/gui/modemctl.glade"),
      goto fail);

  SU_TRYCATCH(suscan_gui_modemctl_clock_load_all_widgets(new), goto fail);

  gtk_builder_connect_signals(new->builder, opaque);

  return new;

fail:
  if (new != NULL)
    suscan_gui_modemctl_clock_destroy(new);

  return NULL;
}

/********************** Modemctl interface boilerplate ***********************/
SUBOOL
suscan_gui_modemctl_clock_applicable(const suscan_config_desc_t *desc)
{
  return suscan_config_desc_has_prefix(desc, SUSCAN_GUI_MODEMCTL_PREFIX);
}

void *
suscan_gui_modemctl_clock_ctor(const suscan_config_t *config, void *opaque)
{
  return suscan_gui_modemctl_clock_new(config, opaque);
}

GtkWidget *
suscan_gui_modemctl_clock_get_root(
    const suscan_gui_modemctl_t *this,
    void *private)
{
  struct suscan_gui_modemctl_clock *clock =
      (struct suscan_gui_modemctl_clock *) private;

  return GTK_WIDGET(clock->root);
}

SUBOOL
suscan_gui_modemctl_clock_get_cb(
    suscan_gui_modemctl_t *this,
    suscan_config_t *config,
    void *private)
{
  struct suscan_gui_modemctl_clock *clock =
      (struct suscan_gui_modemctl_clock *) private;

  return suscan_gui_modemctl_clock_get(clock, config);
}

SUBOOL
suscan_gui_modemctl_clock_set_cb(
    suscan_gui_modemctl_t *this,
    const suscan_config_t *config,
    void *private)
{
  struct suscan_gui_modemctl_clock *clock =
      (struct suscan_gui_modemctl_clock *) private;

  return suscan_gui_modemctl_clock_set(clock, config);
}

void
suscan_gui_modemctl_clock_dtor(void *private)
{
  struct suscan_gui_modemctl_clock *clock =
      (struct suscan_gui_modemctl_clock *) private;

  suscan_gui_modemctl_clock_destroy(clock);
}

/************************** Register AGC class *******************************/
SUBOOL
suscan_gui_modemctl_clock_init(void)
{
  static struct suscan_gui_modemctl_class class = {
      .name       = "clock",
      .applicable = suscan_gui_modemctl_clock_applicable,
      .ctor       = suscan_gui_modemctl_clock_ctor,
      .get_root   = suscan_gui_modemctl_clock_get_root,
      .get        = suscan_gui_modemctl_clock_get_cb,
      .set        = suscan_gui_modemctl_clock_set_cb,
      .dtor       = suscan_gui_modemctl_clock_dtor,
  };

  return suscan_gui_modemctl_class_register(&class);
}
