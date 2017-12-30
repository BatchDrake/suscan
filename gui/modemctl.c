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

#define SU_LOG_DOMAIN "modemctl"

#include "modemctl.h"

PTR_LIST_CONST(struct suscan_gui_modemctl_class, modemctl_class);

void
suscan_gui_modemctl_helper_write_float(GtkEntry *entry, SUFLOAT value)
{
  char number[32];

  snprintf(number, sizeof(number), SUFLOAT_FMT, value);
  gtk_entry_set_text(entry, number);
}

void
suscan_gui_modemctl_helper_try_read_float(GtkEntry *entry, SUFLOAT *result)
{
  const gchar *text;
  SUFLOAT value;

  text = gtk_entry_get_text(entry);
  if (sscanf(text, SUFLOAT_FMT, &value) < 1)
    suscan_gui_modemctl_helper_write_float(entry, *result);
  else
    *result = value;
}


const struct suscan_gui_modemctl_class *
suscan_gui_modemctl_class_lookup(const char *name)
{
  unsigned int i;

  for (i = 0; i < modemctl_class_count; ++i)
    if (strcmp(modemctl_class_list[i]->name, name) == 0)
      return modemctl_class_list[i];

  return NULL;
}

SUBOOL
suscan_gui_modemctl_class_register(
    const struct suscan_gui_modemctl_class *class)
{
  SU_TRYCATCH(class->name != NULL, return SU_FALSE);

  SU_TRYCATCH(
      suscan_gui_modemctl_class_lookup(class->name) == NULL,
      return SU_FALSE);

  SU_TRYCATCH(class->applicable != NULL, return SU_FALSE);
  SU_TRYCATCH(class->ctor != NULL, return SU_FALSE);
  SU_TRYCATCH(class->get_root != NULL, return SU_FALSE);
  SU_TRYCATCH(class->get != NULL, return SU_FALSE);
  SU_TRYCATCH(class->set != NULL, return SU_FALSE);
  SU_TRYCATCH(class->dtor != NULL, return SU_FALSE);

  SU_TRYCATCH(
      PTR_LIST_APPEND_CHECK(modemctl_class, (void *) class) != -1,
      return SU_FALSE);

  return SU_TRUE;
}

suscan_gui_modemctl_t *
suscan_gui_modemctl_new(
    const struct suscan_gui_modemctl_class *class,
    suscan_config_t *config,
    void (*on_update_config) (struct suscan_gui_modemctl *ctl, void *user_data),
    void *user_data)
{
  suscan_gui_modemctl_t *new = NULL;

  SU_TRYCATCH(new = calloc(1, sizeof(suscan_gui_modemctl_t)), goto fail);

  new->class = class;
  new->config = config;
  new->on_update_config = on_update_config;
  new->user_data = user_data;

  SU_TRYCATCH(new->private = (class->ctor) (config, new), goto fail);

  return new;

fail:
  if (new != NULL)
    suscan_gui_modemctl_destroy(new);

  return NULL;
}

GtkWidget *
suscan_gui_modemctl_get_root(const suscan_gui_modemctl_t *ctl)
{
  return (ctl->class->get_root) (ctl, ctl->private);
}

SUBOOL
suscan_gui_modemctl_get(suscan_gui_modemctl_t *ctl)
{
  return (ctl->class->get) (ctl, ctl->config, ctl->private);
}

SUBOOL
suscan_gui_modemctl_set(suscan_gui_modemctl_t *ctl)
{
  return (ctl->class->set) (ctl, ctl->config, ctl->private);
}

void
suscan_gui_modemctl_trigger_update(suscan_gui_modemctl_t *ctl)
{
  if (ctl->on_update_config != NULL)
    (ctl->on_update_config) (ctl, ctl->user_data);
}

void
suscan_gui_modemctl_destroy(suscan_gui_modemctl_t *ctl)
{
  if (ctl->private != NULL)
    (ctl->class->dtor) (ctl->private);

  free(ctl);
}

/* Modemctl set API */
void
suscan_gui_modemctl_set_finalize(struct suscan_gui_modemctl_set *set)
{
  unsigned int i;

  for (i = 0; i < set->modemctl_count; ++i)
    if (set->modemctl_list[i] != NULL)
      suscan_gui_modemctl_destroy(set->modemctl_list[i]);

  if (set->modemctl_list != NULL)
    free(set->modemctl_list);
}

SUBOOL
suscan_gui_modemctl_set_init(
    struct suscan_gui_modemctl_set *set,
    suscan_config_t *config,
    void (*on_update_config) (struct suscan_gui_modemctl *ctl, void *user_data),
    void *user_data)
{
  unsigned int i;
  suscan_gui_modemctl_t *ctl = NULL;

  memset(set, 0, sizeof(struct suscan_gui_modemctl_set));

  for (i = 0; i < modemctl_class_count; ++i)
    if (modemctl_class_list[i]->applicable(config->desc)) {
      SU_TRYCATCH(
          ctl = suscan_gui_modemctl_new(
              modemctl_class_list[i],
              config,
              on_update_config,
              user_data),
          goto fail);

      SU_TRYCATCH(PTR_LIST_APPEND_CHECK(set->modemctl, ctl) != -1, goto fail);

      ctl = NULL;
    }

  return SU_TRUE;

fail:
  if (ctl != NULL)
    suscan_gui_modemctl_destroy(ctl);

  suscan_gui_modemctl_set_finalize(set);

  return SU_FALSE;
}

SUBOOL
suscan_gui_modemctl_set_refresh(struct suscan_gui_modemctl_set *set)
{
  unsigned int i;

  for (i = 0; i < set->modemctl_count; ++i)
    SU_TRYCATCH(
        suscan_gui_modemctl_set(set->modemctl_list[i]),
        return SU_FALSE);

  return SU_TRUE;
}

/****************************** GUI Callbacks ********************************/
void
suscan_gui_modemctl_on_change_generic(GtkWidget *widget, gpointer user_data)
{
  suscan_gui_modemctl_t *ctl = (suscan_gui_modemctl_t *) user_data;

  SU_TRYCATCH(suscan_gui_modemctl_get(ctl), return);

  suscan_gui_modemctl_trigger_update(ctl);
}

void
suscan_gui_modemctl_on_change_event(
    GtkWidget *widget,
    GdkEvent *event,
    gpointer user_data)
{
  suscan_gui_modemctl_t *ctl = (suscan_gui_modemctl_t *) user_data;

  SU_TRYCATCH(suscan_gui_modemctl_get(ctl), return);

  suscan_gui_modemctl_trigger_update(ctl);
}
