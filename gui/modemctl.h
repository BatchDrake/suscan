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

#ifndef _GUI_MODEMCTL_H
#define _GUI_MODEMCTL_H

#include <gtk/gtk.h>
#include <sigutils/sigutils.h>
#include <cfg.h>

struct suscan_gui_modemctl;

struct suscan_gui_modemctl_class {
  const char *name;
  SUBOOL (*applicable) (const suscan_config_desc_t *desc);
  void *(*ctor) (const suscan_config_t *config, void *opaque);

  GtkWidget * (*get_root) (
      const struct suscan_gui_modemctl *this,
      void *private);

  /* Get current configuration */
  SUBOOL (*get) (
        struct suscan_gui_modemctl *this,
        suscan_config_t *config,
        void *private);

  /* Set controls from current configuration */
  SUBOOL (*set) (
      struct suscan_gui_modemctl *this,
      const suscan_config_t *config,
      void *private);

  void (*dtor) (void *private);
};

struct suscan_gui_modemctl {
  const struct suscan_gui_modemctl_class *class;
  void *private; /* Specific modemctl instance */
  suscan_config_t *config; /* Borrowed */
  void *user_data;
  void (*on_update_config) (struct suscan_gui_modemctl *ctl, void *user_data);
  SUBOOL changed_from_code;
};

typedef struct suscan_gui_modemctl suscan_gui_modemctl_t;

struct suscan_gui_modemctl_set {
  PTR_LIST(suscan_gui_modemctl_t, modemctl);
};


/************************ Helper functions ***********************************/
void suscan_gui_modemctl_helper_write_float(GtkEntry *entry, SUFLOAT value);

void suscan_gui_modemctl_helper_try_read_float(
    GtkEntry *entry,
    SUFLOAT *result);

int suscan_gui_modemctl_helper_try_read_combo_id(GtkComboBox *box);

void suscan_gui_modemctl_helper_write_combo_id(GtkComboBox *box, int id);

/************************ Modemctl API ***************************************/
const struct suscan_gui_modemctl_class *suscan_gui_modemctl_class_lookup(
    const char *name);

SUBOOL suscan_gui_modemctl_class_register(
    const struct suscan_gui_modemctl_class *class);

suscan_gui_modemctl_t *suscan_gui_modemctl_new(
    const struct suscan_gui_modemctl_class *class,
    suscan_config_t *config,
    void (*on_update_config) (struct suscan_gui_modemctl *ctl, void *user_data),
    void *user_data);

GtkWidget *suscan_gui_modemctl_get_root(const suscan_gui_modemctl_t *ctl);

SUBOOL suscan_gui_modemctl_get(suscan_gui_modemctl_t *ctl);

SUBOOL suscan_gui_modemctl_set(suscan_gui_modemctl_t *ctl);

void suscan_gui_modemctl_trigger_update(suscan_gui_modemctl_t *ctl);

void suscan_gui_modemctl_destroy(suscan_gui_modemctl_t *ctl);

/* Modemctl set API */
SUBOOL suscan_gui_modemctl_set_init(
    struct suscan_gui_modemctl_set *set,
    suscan_config_t *config,
    void (*on_update_config) (struct suscan_gui_modemctl *ctl, void *user_data),
    void *user_data);

SUBOOL suscan_gui_modemctl_set_refresh(struct suscan_gui_modemctl_set *set);

void suscan_gui_modemctl_set_finalize(struct suscan_gui_modemctl_set *set);

/************************** Registration methods *****************************/
SUBOOL suscan_gui_modemctl_agc_init(void);
SUBOOL suscan_gui_modemctl_afc_init(void);
SUBOOL suscan_gui_modemctl_mf_init(void);
SUBOOL suscan_gui_modemctl_equalizer_init(void);
SUBOOL suscan_gui_modemctl_clock_init(void);

#endif /* _GUI_MODEMCTL_H */
