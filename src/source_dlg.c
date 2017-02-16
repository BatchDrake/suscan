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
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <libgen.h>
#include <ctk.h>
#include "suscan.h"

#define SUSCAN_SOURCE_DIALOG_MAX_BASENAME 15

SUPRIVATE struct ctk_item source_item_list[] = {
    {"BladeRF",    "Nuand's BladeRF", SUSCAN_SOURCE_TYPE_BLADE_RF},
    {"HackRF",     "Great Scott Gadget's HackRF", SUSCAN_SOURCE_TYPE_HACK_RF},
    {"I/Q file",   "Previously recorded I/Q samples", SUSCAN_SOURCE_TYPE_IQ_FILE},
    {"WAV file",   "PCM / WAVE / AIFF audio file", SUSCAN_SOURCE_TYPE_WAV_FILE},
    {"ALSA input", "Read samples from soundcard", SUSCAN_SOURCE_TYPE_ALSA}
};

struct suscan_source_dialog {
  ctk_widget_t *window;
  ctk_widget_t *button;
  ctk_widget_t *selbutton;
  ctk_widget_t *menu;
  ctk_widget_t *file_button;

  SUBOOL        exit_flag;
};

#define suscan_source_dialog_INITIALIZER        \
{                                               \
  NULL, /* window */                            \
  NULL, /* button */                            \
  NULL, /* selbutton */                         \
  NULL, /* menu */                              \
  NULL, /* file_button */                       \
  SU_FALSE, /* exit_flag */                     \
}

SUPRIVATE void
suscan_dialog_on_submit(ctk_widget_t *widget, struct ctk_item *item)
{
  struct suscan_source_dialog *dialog =
      (struct suscan_source_dialog *) ctk_widget_get_private(widget);

  dialog->exit_flag = SU_TRUE;
}

SUPRIVATE void
suscan_dialog_file_on_submit(ctk_widget_t *widget, struct ctk_item *item)
{
  char *result = NULL;
  char *base;
  struct suscan_source_dialog *dialog =
        (struct suscan_source_dialog *) ctk_widget_get_private(widget);

  enum ctk_dialog_response response;

  if ((response = ctk_file_dialog("Open file...", &result))
      == CTK_DIALOG_RESPONSE_ERROR)
    ctk_msgbox(CTK_DIALOG_ERROR, "SUScan", "Failed to open dialog");

  if (response == CTK_DIALOG_RESPONSE_OK) {
    base = basename(result);
    if (strlen(base) > SUSCAN_SOURCE_DIALOG_MAX_BASENAME)
      strncpy(base + SUSCAN_SOURCE_DIALOG_MAX_BASENAME - 3, "...", 4);
    ctk_button_set_caption(dialog->file_button, base);
    free(result);
  }
}

SUPRIVATE void
suscan_source_dialog_finalize(struct suscan_source_dialog *dialog)
{
  if (dialog->file_button != NULL)
    ctk_widget_destroy(dialog->file_button);

  if (dialog->button != NULL)
    ctk_widget_destroy(dialog->button);

  if (dialog->menu != NULL)
    ctk_widget_destroy(dialog->menu);

  if (dialog->selbutton != NULL)
    ctk_widget_destroy(dialog->selbutton);

  if (dialog->window != NULL)
    ctk_widget_destroy(dialog->window);
}

SUPRIVATE SUBOOL
suscan_source_dialog_init(struct suscan_source_dialog *dialog)
{
  struct ctk_widget_handlers hnd;
  unsigned int button_width;

  /* Create Dialog Window */
  if ((dialog->window = ctk_window_new("Open source")) == NULL)
    return SU_TRUE;

  ctk_widget_resize(dialog->window, 33, 15);
  ctk_widget_center(dialog->window);
  ctk_widget_set_shadow(dialog->window, CTK_TRUE);

  /* Create source menu list */
  if ((dialog->menu = ctk_menu_new(NULL, 0, 0)) == NULL)
    return SU_TRUE;

  if (!ctk_menu_add_multiple_items(
      dialog->menu,
      source_item_list,
      ARRAY_SZ(source_item_list)))
    return SU_TRUE;

  /* Create source selection button */
  mvwaddstr(dialog->window->c_window, 2, 2, "Source type:");
  if ((dialog->selbutton
      = ctk_selbutton_new(dialog->window, 15, 2, dialog->menu)) == NULL)
    return SU_TRUE;
  ctk_widget_set_attrs(dialog->selbutton, COLOR_PAIR(CTK_CP_TEXTAREA));

  /* Create file selection button */
  mvwaddstr(dialog->window->c_window, 4, 2, "File:");
  if ((dialog->file_button
      = ctk_button_new(dialog->window, 15, 4, "Browse...")) == NULL)
    return SU_TRUE;
  ctk_widget_set_attrs(dialog->file_button, COLOR_PAIR(CTK_CP_TEXTAREA));
  ctk_widget_set_private(dialog->file_button, dialog);
  ctk_widget_resize(dialog->file_button, SUSCAN_SOURCE_DIALOG_MAX_BASENAME, 1);

  /* Okay button */
  button_width = 10;

  if ((dialog->button = ctk_button_new(
      dialog->window,
      dialog->window->width / 2 - button_width / 2,
      13, "OK")) == NULL)
    return SU_TRUE;
  ctk_widget_set_attrs(dialog->button, COLOR_PAIR(CTK_CP_TEXTAREA));
  ctk_widget_set_private(dialog->button, dialog);

  ctk_widget_get_handlers(dialog->button, &hnd);
  hnd.submit_handler = suscan_dialog_on_submit;
  ctk_widget_set_handlers(dialog->button, &hnd);

  ctk_widget_get_handlers(dialog->file_button, &hnd);
  hnd.submit_handler = suscan_dialog_file_on_submit;
  ctk_widget_set_handlers(dialog->file_button, &hnd);

  ctk_widget_show(dialog->file_button);
  ctk_widget_show(dialog->selbutton);
  ctk_widget_show(dialog->button);
  ctk_widget_show(dialog->window);

  ctk_window_focus_next(dialog->window);

  ctk_update();

  return SU_TRUE;
}

SUBOOL
suscan_open_source_dialog(void)
{
  struct suscan_source_dialog dialog = suscan_source_dialog_INITIALIZER;
  int c;
  SUBOOL ok = SU_FALSE;

  if (!suscan_source_dialog_init(&dialog))
    goto done;

  while (!dialog.exit_flag) {
    c = getch();
    if (c == 'q')
      break;

    ctk_widget_notify_kbd(dialog.window, c);
    ctk_update();
  }

  ctk_widget_hide(dialog.window);

  ok = SU_TRUE;

done:
  suscan_source_dialog_finalize(&dialog);

  ctk_update();

  return ok;
}
