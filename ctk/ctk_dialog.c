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
#include <sys/types.h>
#include <sys/stat.h>
#include <libgen.h>
#include <unistd.h>
#include <dirent.h>

#include "ctk.h"

/************************** File chooser dialog ******************************/
struct ctk_file_dialog {
  ctk_widget_t *window;
  ctk_widget_t *dir_menu;
  ctk_widget_t *file_menu;
  ctk_widget_t *ok_button;
  ctk_widget_t *cancel_button;
  ctk_widget_t *path_entry;

  char *curr_path; /* Overrides everything else */
  char *curr_directory;

  CTKBOOL exit_flag;
  CTKBOOL cancel;
};

#define ctk_file_dialog_INITIALIZER \
  {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, CTK_FALSE, CTK_FALSE}

/*
 * Couldn't find a way to span the selection bar to the right end of the
 * menu subwindow. This will have to do the job.
 *
 * NCurses is maybe one of the worst libraries I've ever dealt with.
 */
#define CTK_DIALOG_RIGHT_PADDING "                                       "

CTKPRIVATE CTKBOOL ctk_file_dialog_set_path(
    struct ctk_file_dialog *dialog,
    const char *path);

CTKPRIVATE CTKBOOL
ctk_file_dialog_is_file_selected(const struct ctk_file_dialog *dialog)
{
  return ctk_menu_get_current_item(dialog->file_menu) != NULL;
}

CTKPRIVATE char *
ctk_file_dialog_get_selected_file(const struct ctk_file_dialog *dialog)
{
  const struct ctk_item *item = NULL;

  if (dialog->curr_path != NULL)
    return strdup(dialog->curr_path);

  if (dialog->curr_directory == NULL)
    return NULL;

  if (ctk_menu_get_item_count(dialog->file_menu) == 0)
    return NULL;

  if ((item = ctk_menu_get_current_item(dialog->file_menu)) == NULL)
    return NULL;

  return strbuild(
      "%s/%s",
      strcmp(dialog->curr_directory, "/") == 0 ? "" : dialog->curr_directory,
      item->name);
}

CTKPRIVATE void
ctk_file_dialog_on_submit_ok(ctk_widget_t *widget, struct ctk_item *item)
{
  struct ctk_file_dialog *dialog =
      (struct ctk_file_dialog *) ctk_widget_get_private(widget);

  if (ctk_file_dialog_is_file_selected(dialog))
    dialog->exit_flag = CTK_TRUE;
}

CTKPRIVATE void
ctk_file_dialog_on_submit_cancel(ctk_widget_t *widget, struct ctk_item *item)
{
  struct ctk_file_dialog *dialog =
      (struct ctk_file_dialog *) ctk_widget_get_private(widget);

  dialog->exit_flag = CTK_TRUE;
  dialog->cancel = CTK_TRUE;
}

CTKPRIVATE void
ctk_file_dialog_on_submit_file(ctk_widget_t *widget, struct ctk_item *item)
{
  struct ctk_file_dialog *dialog =
      (struct ctk_file_dialog *) ctk_widget_get_private(widget);

  dialog->exit_flag = CTK_TRUE;
}

CTKPRIVATE void
ctk_file_dialog_on_submit_dir(ctk_widget_t *widget, struct ctk_item *item)
{
  struct ctk_file_dialog *dialog =
      (struct ctk_file_dialog *) ctk_widget_get_private(widget);
  char *path;
  char *effective_path;
  char *message;

  /* We can safely ignore this */
  if (strcmp(item->name, ".") == 0)
    return;

  if (strcmp(item->name, "..") == 0) {
    if ((path = strdup(dialog->curr_directory)) != NULL)
      effective_path = dirname(path);
  } else {
    path = strbuild(
        "%s/%s",
        strcmp(dialog->curr_directory, "/") == 0 ? "" : dialog->curr_directory,
        item->name);
    effective_path = path;
  }

  if (path == NULL) {
    ctk_msgbox(CTK_DIALOG_ERROR, "Out of memory", message);
    return;
  }

  if (!ctk_file_dialog_set_path(dialog, effective_path))
    if ((message = strbuild(
        "Cannot open directory `%s': %s",
        effective_path,
        strerror(errno))) != NULL) {
      ctk_msgbox(CTK_DIALOG_ERROR, "Open directory", message);
      free(message);
    }

  free(path);
}

CTKPRIVATE void
ctk_file_dialog_on_kbd_file(ctk_widget_t *widget, int c)
{
  char *fullpath;
  struct ctk_file_dialog *dialog =
        (struct ctk_file_dialog *) ctk_widget_get_private(widget);

  if (c == ' ') {
    if ((fullpath = ctk_file_dialog_get_selected_file(dialog)) != NULL) {
      ctk_entry_set_text(dialog->path_entry, fullpath);
      free(fullpath);
    }
  } else {
    ctk_menu_on_kbd(widget, c);
  }
}

CTKPRIVATE void
ctk_file_dialog_on_submit_path(ctk_widget_t *widget, struct ctk_item *item)
{
  struct ctk_file_dialog *dialog =
      (struct ctk_file_dialog *) ctk_widget_get_private(widget);
  char *message;
  const char *path;
  struct stat sbuf;

  path = ctk_entry_get_text(dialog->path_entry);

  /*
   * If user has entered the full path of a regular file, use this as
   * the result of the dialog.
   */
  if (stat(path, &sbuf) != -1 && S_ISREG(sbuf.st_mode)) {
    if ((dialog->curr_path = strdup(path)) == NULL) {
      ctk_msgbox(CTK_DIALOG_ERROR, "Open file", "Out of memory");
      return;
    }

    dialog->exit_flag = CTK_TRUE;
    return;
  }

  if (!ctk_file_dialog_set_path(dialog, path))
    if ((message = strbuild(
        "Cannot open directory `%s': %s",
        path,
        strerror(errno))) != NULL) {
      ctk_msgbox(CTK_DIALOG_ERROR, "Open directory", message);
      free(message);
      if (dialog->curr_directory != NULL)
        ctk_entry_set_text(dialog->path_entry, dialog->curr_directory);
    }
}

CTKPRIVATE void
ctk_file_dialog_finalize(struct ctk_file_dialog *dialog)
{
  if (dialog->window != NULL)
    ctk_widget_hide(dialog->window);

  if (dialog->cancel_button != NULL)
    ctk_widget_destroy(dialog->cancel_button);

  if (dialog->ok_button != NULL)
    ctk_widget_destroy(dialog->ok_button);

  if (dialog->file_menu != NULL)
    ctk_widget_destroy(dialog->file_menu);

  if (dialog->dir_menu != NULL)
    ctk_widget_destroy(dialog->dir_menu);

  if (dialog->path_entry != NULL)
    ctk_widget_destroy(dialog->path_entry);

  if (dialog->window != NULL)
    ctk_widget_destroy(dialog->window);

  if (dialog->curr_directory != NULL)
    free(dialog->curr_directory);

  if (dialog->curr_path != NULL)
    free(dialog->curr_path);
}

CTKPRIVATE int
__ctk_file_dialog_cmp(const struct ctk_item *a, const struct ctk_item *b)
{
  if (strcmp(a->name, ".") == 0)
    return -1;

  if (strcmp(a->name, "..") == 0) {
    if (strcmp(b->name, ".") == 0)
      return 1;

    return -1;
  }

  return strcmp(a->name, b->name);
}

CTKPRIVATE CTKBOOL
ctk_file_dialog_set_path(struct ctk_file_dialog *dialog, const char *path)
{
  struct stat sbuf;
  CTKBOOL ok = CTK_FALSE;
  char *fullpath = NULL;
  char *pathdup;
  struct dirent *ent;
  DIR *d = NULL;

  if (stat(path, &sbuf) == -1)
    goto done;

  if (!S_ISDIR(sbuf.st_mode))
    goto done;

  if ((d = opendir(path)) == NULL)
    goto done;

  if ((pathdup = strdup(path)) == NULL)
    goto done;

  if (dialog->curr_directory != NULL)
    free(dialog->curr_directory);
  dialog->curr_directory = pathdup;

  ctk_menu_clear(dialog->dir_menu);
  ctk_menu_clear(dialog->file_menu);

  if (!ctk_entry_set_text(dialog->path_entry, path))
    goto done;

  while ((ent = readdir(d)) != NULL) {
    if ((fullpath = strbuild("%s/%s", path, ent->d_name)) == NULL)
      goto done;

    if (stat(fullpath, &sbuf) != -1) {
      if (S_ISDIR(sbuf.st_mode)) {
        if (!__ctk_menu_add_item(
            dialog->dir_menu,
            ent->d_name,
            CTK_DIALOG_RIGHT_PADDING,
            dialog))
          goto done;
      } else {
        if (!__ctk_menu_add_item(
            dialog->file_menu,
            ent->d_name,
            CTK_DIALOG_RIGHT_PADDING,
            dialog))
          goto done;
      }
    }

    free(fullpath);
    fullpath = NULL;
  }

  __ctk_menu_update(dialog->dir_menu);
  __ctk_menu_update(dialog->file_menu);

  ctk_menu_sort(dialog->dir_menu, __ctk_file_dialog_cmp);
  ctk_menu_sort(dialog->file_menu, __ctk_file_dialog_cmp);

  ctk_update();

  ok = CTK_TRUE;

done:
  if (fullpath != NULL)
    free(fullpath);

  if (d != NULL)
    closedir(d);

  return ok;
}

CTKPRIVATE CTKBOOL
ctk_file_dialog_init(struct ctk_file_dialog *dialog, const char *title)
{
  struct ctk_widget_handlers hnd;

  /* Create dialog Window */
  if ((dialog->window = ctk_window_new(title)) == NULL)
    return CTK_FALSE;
  ctk_widget_set_private(dialog->window, dialog);
  ctk_widget_set_shadow(dialog->window, CTK_TRUE);

  if (!ctk_widget_resize(
      dialog->window,
      CTK_DIALOG_FILE_CHOOSER_WIDTH,
      CTK_DIALOG_FILE_CHOOSER_HEIGHT))
    return CTK_FALSE;

  ctk_widget_center(dialog->window);

  /* Create path entry */
  if ((dialog->path_entry = ctk_entry_new(
      dialog->window,
      CTK_DIALOG_FILE_PATH_X,
      CTK_DIALOG_FILE_PATH_Y,
      CTK_DIALOG_FILE_CHOOSER_WIDTH - 2 * CTK_DIALOG_FILE_PATH_X)) == NULL)
    return CTK_FALSE;
  ctk_widget_set_private(dialog->path_entry, dialog);

  /* Create directory chooser */
  if ((dialog->dir_menu = ctk_menu_new(
      dialog->window,
      CTK_DIALOG_FILE_DIR_X,
      CTK_DIALOG_FILE_DIR_Y)) == NULL)
    return CTK_FALSE;
  ctk_widget_set_private(dialog->dir_menu, dialog);
  ctk_menu_set_autoresize(dialog->dir_menu, CTK_FALSE);
  ctk_widget_set_shadow(dialog->dir_menu, CTK_FALSE);

  if (!ctk_widget_resize(
      dialog->dir_menu,
      CTK_DIALOG_FILE_DIR_WIDTH,
      CTK_DIALOG_FILE_DIR_HEIGHT))
    return CTK_FALSE;

  if (!ctk_menu_add_item(dialog->dir_menu, ".", CTK_DIALOG_RIGHT_PADDING, NULL))
    return CTK_FALSE;

  if (!ctk_menu_add_item(dialog->dir_menu, "..", CTK_DIALOG_RIGHT_PADDING, NULL))
    return CTK_FALSE;

  /* Create file chooser */
  if ((dialog->file_menu = ctk_menu_new(
      dialog->window,
      CTK_DIALOG_FILE_FILENAME_X,
      CTK_DIALOG_FILE_FILENAME_Y)) == NULL)
    return CTK_FALSE;
  ctk_widget_set_private(dialog->file_menu, dialog);
  ctk_menu_set_autoresize(dialog->file_menu, CTK_FALSE);
  ctk_widget_set_shadow(dialog->file_menu, CTK_FALSE);

  if (!ctk_widget_resize(
      dialog->file_menu,
      CTK_DIALOG_FILE_FILENAME_WIDTH,
      CTK_DIALOG_FILE_FILENAME_HEIGHT))
    return CTK_FALSE;

  if (!ctk_menu_add_item(
      dialog->file_menu,
      "<no file>",
      CTK_DIALOG_RIGHT_PADDING, NULL))
    return CTK_FALSE;

  if ((dialog->cancel_button = ctk_button_new(
      dialog->window,
    CTK_DIALOG_FILE_CANCEL_BUTTON_X,
    CTK_DIALOG_FILE_CANCEL_BUTTON_Y,
    "Cancel")) == NULL)
    return CTK_FALSE;
  ctk_widget_set_private(dialog->cancel_button, dialog);
  ctk_widget_set_attrs(dialog->cancel_button, COLOR_PAIR(CTK_CP_TEXTAREA));

  if ((dialog->ok_button = ctk_button_new(
      dialog->window,
      CTK_DIALOG_FILE_OK_BUTTON_X,
      CTK_DIALOG_FILE_OK_BUTTON_Y,
      "OK")) == NULL)
    return CTK_FALSE;
  ctk_widget_set_private(dialog->ok_button, dialog);
  ctk_widget_set_attrs(dialog->ok_button, COLOR_PAIR(CTK_CP_TEXTAREA));

  /* Set submit handlers */
  ctk_widget_get_handlers(dialog->ok_button, &hnd);
  hnd.submit_handler = ctk_file_dialog_on_submit_ok;
  ctk_widget_set_handlers(dialog->ok_button, &hnd);

  ctk_widget_get_handlers(dialog->cancel_button, &hnd);
  hnd.submit_handler = ctk_file_dialog_on_submit_cancel;
  ctk_widget_set_handlers(dialog->cancel_button, &hnd);

  ctk_widget_get_handlers(dialog->file_menu, &hnd);
  hnd.submit_handler = ctk_file_dialog_on_submit_file;
  hnd.kbd_handler    = ctk_file_dialog_on_kbd_file;
  ctk_widget_set_handlers(dialog->file_menu, &hnd);

  ctk_widget_get_handlers(dialog->dir_menu, &hnd);
  hnd.submit_handler = ctk_file_dialog_on_submit_dir;
  ctk_widget_set_handlers(dialog->dir_menu, &hnd);

  ctk_widget_get_handlers(dialog->path_entry, &hnd);
  hnd.submit_handler = ctk_file_dialog_on_submit_path;
  ctk_widget_set_handlers(dialog->path_entry, &hnd);

  /* Show all */
  ctk_widget_show(dialog->ok_button);
  ctk_widget_show(dialog->cancel_button);
  ctk_widget_show(dialog->file_menu);
  ctk_widget_show(dialog->dir_menu);
  ctk_widget_show(dialog->path_entry);

  ctk_widget_show(dialog->window);

  ctk_window_set_focus(dialog->window, dialog->file_menu);

  ctk_update();

  return CTK_TRUE;
}

enum ctk_dialog_response
ctk_file_dialog(const char *title, char **file)
{
  struct ctk_file_dialog dialog = ctk_file_dialog_INITIALIZER;
  enum ctk_dialog_response resp = CTK_DIALOG_RESPONSE_ERROR;
  char *cwd = NULL;
  int c;

  if ((cwd = malloc(PATH_MAX)) == NULL)
      goto done;

  if (getcwd(cwd, PATH_MAX) == NULL)
    goto done;

  if (!ctk_file_dialog_init(&dialog, title))
    goto done;

  if (!ctk_file_dialog_set_path(&dialog, cwd))
    goto done;

  free(cwd);
  cwd = NULL;

  while (!dialog.exit_flag && (c = getch()) != CTK_KEY_ESCAPE) {
    ctk_widget_notify_kbd(dialog.window, c);
    ctk_update();
  }

  if (dialog.cancel) {
    resp = CTK_DIALOG_RESPONSE_CANCEL;
    goto done;
  }

  if (ctk_menu_get_item_count(dialog.file_menu) == 0
      && dialog.curr_path == NULL) {
    resp = CTK_DIALOG_RESPONSE_CANCEL;
    goto done;
  }

  if ((*file = ctk_file_dialog_get_selected_file(&dialog)) == NULL) {
    resp = CTK_DIALOG_RESPONSE_ERROR;
    goto done;
  }

  resp = CTK_DIALOG_RESPONSE_OK;

done:
  ctk_file_dialog_finalize(&dialog);

  if (cwd != NULL)
    free(cwd);

  return resp;
}

/*************************** Message box dialog ******************************/
CTKPRIVATE void
ctk_dialog_get_text_size(
    const char *text,
    unsigned int *width,
    unsigned int *height)
{
  unsigned int rows = 1;
  unsigned int cols = 0;
  unsigned int max_cols = 0;


  while (*text) {
    if (*text++ == '\n') {
      ++rows;
      cols = 0;
    } else {
      if (++cols > max_cols)
        max_cols = cols;
    }
  }

  *width = max_cols;
  *height = rows;
}

CTKBOOL
ctk_msgbox(enum ctk_dialog_kind kind, const char *title, const char *msg)
{
  ctk_widget_t *window = NULL;
  ctk_widget_t *button = NULL;
  unsigned int text_width, text_height;
  unsigned int i, row = 0, col = 0;
  unsigned int win_width, win_height;
  unsigned int button_width;
  int c;

  CTKBOOL ok = CTK_FALSE;

  if ((window = ctk_window_new(title)) == NULL)
    goto done;

  ctk_widget_set_shadow(window, CTK_TRUE);

  ctk_widget_set_attrs(window, COLOR_PAIR(kind + 6));

  ctk_dialog_get_text_size(msg, &text_width, &text_height);

  if (text_height < 1)
    text_height = 1;

  win_width = CTK_DIALOG_MSGBOX_MIN_WIDTH;

  if (text_width + CTK_DIALOG_MSGBOX_HORIZONTAL_PADDING > win_width)
    win_width = text_width + CTK_DIALOG_MSGBOX_HORIZONTAL_PADDING;

  if (win_width > CTK_DIALOG_MSGBOX_MAX_WIDTH)
    win_width = CTK_DIALOG_MSGBOX_MAX_WIDTH;

  win_height = text_height + CTK_DIALOG_MSGBOX_VERTICAL_PADDING;

  if (!ctk_widget_resize(window, win_width, win_height))
    goto done;

  if (!ctk_widget_center(window))
    goto done;

  for (i = 0; i < strlen(msg); ++i) {
    if (msg[i] == '\n') {
      ++row;
      col = 0;
    } else {
      mvwaddch(window->c_window, row + 2, col++ + 2, msg[i]);
      if (col
          == CTK_DIALOG_MSGBOX_MAX_WIDTH
          - CTK_DIALOG_MSGBOX_HORIZONTAL_PADDING) {
        ++row;
        col = 0;
      }
    }
  }


  button_width = 10;

  if ((button = ctk_button_new(
      window,
      win_width / 2 - button_width / 2,
      row + 4, "OK")) == NULL)
    goto done;

  ctk_widget_set_attrs(button, COLOR_PAIR(CTK_CP_TEXTAREA));

  ctk_widget_show(button);
  ctk_widget_show(window);

  ctk_update();

  while ((c = getch()) != '\n') {
    ctk_widget_notify_kbd(window, c);
    ctk_update();
  }

  ctk_widget_hide(window);

  ok = CTK_TRUE;

done:
  if (button != NULL)
    ctk_widget_destroy(button);

  if (window != NULL)
    ctk_widget_destroy(window);

  update_panels();
  doupdate();

  return ok;
}
