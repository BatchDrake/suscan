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

#include <string.h>
#include <time.h>

#define SU_LOG_DOMAIN "inspector-gui-callbacks"

#include <sigutils/agc.h>
#include <codec/codec.h>

#include "gui.h"
#include "inspector.h"

void
suscan_gui_inspector_save_as_cb(GtkWidget *widget, gpointer data)
{
  suscan_gui_inspector_t *inspector = (suscan_gui_inspector_t *) data;
  suscan_gui_t *gui = suscan_gui_symsrc_get_gui((suscan_gui_symsrc_t *) data);
  suscan_object_t *object = NULL;
  suscan_object_t *existing = NULL;
  const char *name;

  name = suscan_gui_prompt(
      gui,
      "Save inspector",
      "Enter inspector name",
      "");

  if (name != NULL) {
    if ((existing = suscan_gui_demod_lookup(gui, name)) != NULL) {
      if (!suscan_gui_yes_or_no(
          gui,
          "Replace demodulator",
          "There is already a demodulator named `%s'. Do you want to replace it?",
          name))
        goto done;
    }

    SU_TRYCATCH(
        object = suscan_gui_inspector_serialize(inspector),
        goto done);

    SU_TRYCATCH(
        suscan_gui_inspector_set_label(inspector, name),
        goto done);

    SU_TRYCATCH(suscan_gui_demod_append(gui, name, object), goto done);

    object = NULL;
  }

done:
  if (object != NULL)
    suscan_object_destroy(object);
}

void
suscan_gui_inspector_save_cb(GtkWidget *widget, gpointer data)
{
  suscan_gui_inspector_t *inspector = (suscan_gui_inspector_t *) data;
  suscan_gui_t *gui = suscan_gui_symsrc_get_gui((suscan_gui_symsrc_t *) data);
  suscan_object_t *object = NULL;
  suscan_object_t *existing = NULL;

  /* No inspector label? This should behave as "save as" */
  if (inspector->label == NULL) {
    suscan_gui_inspector_save_as_cb(widget, data);
  } else {
    SU_TRYCATCH(
        object = suscan_gui_inspector_serialize(inspector),
        goto done);

    if ((existing = suscan_gui_demod_lookup(gui, inspector->label)) != NULL)
      (void) suscan_gui_demod_remove(gui, existing);

    SU_TRYCATCH(
        suscan_gui_demod_append(gui, inspector->label, object),
        goto done);

    object = NULL;
  }

done:
  if (object != NULL)
    suscan_object_destroy(object);
}

void
suscan_gui_inspector_open_cb(GtkWidget *widget, gpointer data)
{
  const suscan_object_t *selected;
  char *page_label = NULL;
  const char *class, *label;
  suscan_gui_inspector_t *inspector = (suscan_gui_inspector_t *) data;
  suscan_gui_t *gui = suscan_gui_symsrc_get_gui((suscan_gui_symsrc_t *) data);

  if (inspector->inshnd == -1) {
    suscan_error(
        gui,
        "Cannot open inspector configuration",
        "Cannot apply configuration when inspector is idle");
  } else if ((selected = suscan_gui_ask_for_demod(gui)) != NULL) {
    class = suscan_object_get_field_value(selected, "class");
    if (class == NULL) {
      suscan_error(
          gui,
          "Cannot open inspector configuration",
          "Inspector configuration has no class");
      goto done;
    }

    if (strcmp(inspector->class, class) != 0) {
      suscan_error(
          gui,
          "Cannot open inspector configuration",
          "Cannot apply a %s configuration to a %s inspector",
          class,
          inspector->class);
      goto done;
    }

    if (!suscan_gui_inspector_deserialize(inspector, selected)) {
      suscan_error(
          gui,
          "Cannot open inspector configuration",
          "Cannot apply configuration to the current inspector (see log)");
      goto done;
    }

    if ((label = suscan_object_get_field_value(selected, "label")) == NULL)
      label = "Unnamed demodulator";

    SU_TRYCATCH(
        page_label = strbuild(
            "%s at %lli Hz",
            label,
            (uint64_t) round(inspector->channel.fc)),
        goto done);

    gtk_label_set_text(inspector->pageLabel, page_label);
  }

done:
  if (page_label != NULL) {
    free(page_label);
  }
}

void
suscan_inspector_on_scroll(GtkWidget *widget, gpointer data)
{
  suscan_gui_inspector_t *inspector = (suscan_gui_inspector_t *) data;

  sugtk_sym_view_set_offset(
      inspector->symbolView,
      floor(gtk_adjustment_get_value(inspector->symViewScrollAdjustment))
      * sugtk_sym_view_get_width(inspector->symbolView));
}

void
suscan_inspector_on_change_spectrum(GtkWidget *widget, gpointer data)
{
  suscan_gui_inspector_t *inspector = (suscan_gui_inspector_t *) data;
  int id;

  id = suscan_gui_modemctl_helper_try_read_combo_id(
      GTK_COMBO_BOX(inspector->spectrumSourceComboBoxText));

  suscan_analyzer_inspector_set_spectrum_async(
      inspector->_parent.gui->analyzer,
      inspector->inshnd,
      id,
      rand());

  sugtk_spectrum_reset(inspector->spectrum);
}

void
suscan_inspector_on_spectrum_center(GtkWidget *widget, gpointer data)
{
  suscan_gui_inspector_t *inspector = (suscan_gui_inspector_t *) data;

  sugtk_spectrum_reset(inspector->spectrum);
}

void
suscan_inspector_on_spectrum_reset(GtkWidget *widget, gpointer data)
{
  suscan_gui_inspector_t *inspector = (suscan_gui_inspector_t *) data;

  sugtk_spectrum_set_freq_offset(inspector->spectrum, 0);
}

void
suscan_inspector_on_toggle_spectrum_autolevel(GtkWidget *widget, gpointer data)
{
  suscan_gui_inspector_t *inspector = (suscan_gui_inspector_t *) data;

  sugtk_spectrum_set_auto_level(
      inspector->spectrum,
      gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)));
}

void
suscan_inspector_on_toggle_spectrum_mode(GtkWidget *widget, gpointer data)
{
  suscan_gui_inspector_t *inspector = (suscan_gui_inspector_t *) data;
  SUBOOL use_wf = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));

  if (use_wf) {
    sugtk_spectrum_set_mode(
        inspector->spectrum,
        SUGTK_SPECTRUM_MODE_WATERFALL);
    gtk_button_set_label(GTK_BUTTON(widget), "Waterfall");
  } else {
    sugtk_spectrum_set_mode(
        inspector->spectrum,
        SUGTK_SPECTRUM_MODE_SPECTROGRAM);
    gtk_button_set_label(GTK_BUTTON(widget), "Spectrogram");
  }
}

/************************** Inspector tab callbacks **************************/
void
suscan_on_close_inspector_tab(GtkWidget *widget, gpointer data)
{
  suscan_gui_inspector_t *insp = (suscan_gui_inspector_t *) data;

  if (!insp->dead) {
    /*
     * Inspector is not dead: send a close signal and wait for analyzer
     * response to close it
     */
    suscan_gui_inspector_close(insp);
  } else {
    /*
     * Inspector is dead (because its analyzer has disappeared). Just
     * remove the page and free allocated memory
     */
    suscan_gui_remove_inspector(insp->_parent.gui, insp);

    suscan_gui_inspector_destroy(insp);
  }
}

void
suscan_inspector_on_save(
    GtkWidget *widget,
    gpointer data)
{
  suscan_gui_inspector_t *insp = (suscan_gui_inspector_t *) data;
  char *new_fname = NULL;

  SU_TRYCATCH(
      new_fname = suscan_gui_inspector_to_filename(insp, "symbols", ".log"),
      goto done);

  SU_TRYCATCH(
      sugtk_sym_view_save_helper(
          insp->symbolView,
          "Save symbol view",
          new_fname,
          suscan_gui_inspector_get_bits(insp)),
      goto done);

done:
  if (new_fname != NULL)
    free(new_fname);
}

void
suscan_inspector_on_toggle_record(
    GtkWidget *widget,
    gpointer data)
{
  suscan_gui_inspector_t *insp = (suscan_gui_inspector_t *) data;

  insp->recording = gtk_toggle_tool_button_get_active(
      GTK_TOGGLE_TOOL_BUTTON(widget));
}

void
suscan_inspector_on_clear(
    GtkWidget *widget,
    gpointer data)
{
  suscan_gui_inspector_t *insp = (suscan_gui_inspector_t *) data;

  sugtk_sym_view_clear(insp->symbolView);
}

void
suscan_inspector_on_zoom_in(
    GtkWidget *widget,
    gpointer data)
{
  suscan_gui_inspector_t *insp = (suscan_gui_inspector_t *) data;
  guint curr_width = sugtk_sym_view_get_width(insp->symbolView);
  guint curr_zoom = sugtk_sym_view_get_zoom(insp->symbolView);

  curr_zoom <<= 1;

  if (curr_width < curr_zoom)
    curr_zoom = curr_width;

  sugtk_sym_view_set_zoom(insp->symbolView, curr_zoom);
}


void
suscan_inspector_on_zoom_out(
    GtkWidget *widget,
    gpointer data)
{
  suscan_gui_inspector_t *insp = (suscan_gui_inspector_t *) data;
  guint curr_width = sugtk_sym_view_get_width(insp->symbolView);
  guint curr_zoom = sugtk_sym_view_get_zoom(insp->symbolView);

  curr_zoom >>= 1;

  if (curr_zoom < 1)
    curr_zoom = 1;

  sugtk_sym_view_set_zoom(insp->symbolView, curr_zoom);
}

void
suscan_inspector_on_toggle_autoscroll(
    GtkWidget *widget,
    gpointer data)
{
  suscan_gui_inspector_t *insp = (suscan_gui_inspector_t *) data;
  gboolean active;

  active = gtk_toggle_tool_button_get_active(GTK_TOGGLE_TOOL_BUTTON(widget));

  sugtk_sym_view_set_autoscroll(insp->symbolView, active);
  gtk_widget_set_sensitive(GTK_WIDGET(insp->offsetSpinButton), !active);
}

void
suscan_inspector_on_toggle_autofit(
    GtkWidget *widget,
    gpointer data)
{
  suscan_gui_inspector_t *insp = (suscan_gui_inspector_t *) data;
  gboolean active;

  active = gtk_toggle_tool_button_get_active(GTK_TOGGLE_TOOL_BUTTON(widget));

  sugtk_sym_view_set_autofit(insp->symbolView, active);
  gtk_widget_set_sensitive(GTK_WIDGET(insp->widthSpinButton), !active);
}

void
suscan_inspector_on_set_offset(
    GtkWidget *widget,
    gpointer data)
{
  suscan_gui_inspector_t *insp = (suscan_gui_inspector_t *) data;

  if (!gtk_toggle_tool_button_get_active(
      GTK_TOGGLE_TOOL_BUTTON(insp->autoScrollToggleButton)))
    sugtk_sym_view_set_offset(
        insp->symbolView,
        gtk_spin_button_get_value(insp->offsetSpinButton));
}

void
suscan_inspector_on_set_width(
    GtkWidget *widget,
    gpointer data)
{
  suscan_gui_inspector_t *insp = (suscan_gui_inspector_t *) data;

  if (!gtk_toggle_tool_button_get_active(
      GTK_TOGGLE_TOOL_BUTTON(insp->autoFitToggleButton)))
    sugtk_sym_view_set_width(
        insp->symbolView,
        gtk_spin_button_get_value(insp->widthSpinButton));
}

void
suscan_gui_inspector_on_reshape(GtkWidget *widget, gpointer data)
{
  suscan_gui_inspector_t *insp = (suscan_gui_inspector_t *) data;

  suscan_gui_inspector_update_spin_buttons(insp);
}


