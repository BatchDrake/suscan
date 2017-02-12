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

#include "ctk.h"

/***************************** CTK Item API **********************************/
void
ctk_item_destroy(struct ctk_item *item)
{
  if (item->name != NULL)
    free(item->name);

  if (item->desc != NULL)
    free(item->desc);

  free(item);
}

struct ctk_item *
ctk_item_new(const char *name, const char *desc, void *private)
{
  struct ctk_item *new = NULL;

  if ((new = calloc(1, sizeof (struct ctk_item))) == NULL)
    goto fail;

  if ((new->name = strdup(name)) == NULL)
    goto fail;

  if ((new->desc = strdup(desc)) == NULL)
    goto fail;

  new->private = private;

  return new;

fail:
  if (new != NULL)
    ctk_item_destroy(new);

  return NULL;
}

struct ctk_item *
ctk_item_dup(const struct ctk_item *item)
{
  return ctk_item_new(item->name, item->desc, item->private);
}

/**************************** Misc functions *********************************/
int
ctk_widget_lookup_index_by_accel(
    ctk_widget_t * const *list,
    unsigned int count,
    int accel)
{
  unsigned int i;

  for (i = 0; i < count; ++i)
    if (list[i] != NULL)
      if (list[i]->accel == tolower(accel))
        return i;

  return -1;
}

/***************************** CTK Widget API ********************************/
void
ctk_widget_set_accel(ctk_widget_t *widget, int accel)
{
  widget->accel = tolower(accel);
}

int
ctk_widget_get_accel(const ctk_widget_t *widget)
{
  return widget->accel;
}

CTKPRIVATE CTKBOOL
ctk_widget_attach(ctk_widget_t *widget, ctk_widget_t *child)
{
  if (widget->handlers.attach_handler != NULL)
    return (widget->handlers.attach_handler) (widget, child);

  /*
   * If widget doesn't support attachment of child widgets, this
   * call must fail
   */
  return CTK_FALSE;
}

CTKPRIVATE void
ctk_widget_detach(ctk_widget_t *widget, ctk_widget_t *child)
{
  if (widget->handlers.detach_handler != NULL)
    (widget->handlers.detach_handler) (widget, child);

}

void
ctk_widget_focus(ctk_widget_t *widget)
{
  if (widget->handlers.focus_handler != NULL)
    return (widget->handlers.focus_handler) (widget);
}

void
ctk_widget_submit(ctk_widget_t *widget, struct ctk_item *item)
{
  if (widget->handlers.submit_handler != NULL)
    return (widget->handlers.submit_handler) (widget, item);
}

void
ctk_widget_blur(ctk_widget_t *widget)
{
  if (widget->handlers.blur_handler != NULL)
    (widget->handlers.blur_handler) (widget);
}

CTKPRIVATE void
ctk_widget_fill_shadow(ctk_widget_t *widget)
{
  unsigned int i, j;
  unsigned int x, y;
  chtype attr;

  for (j = 0; j < widget->height; ++j)
    for (i = 0; i < widget->width; ++i) {
      x = i + widget->x + CTK_WIDGET_SHADOW_DX;
      y = j + widget->y + CTK_WIDGET_SHADOW_DY;

      attr = A_COLOR & (
          widget->root == NULL
          ? mvwinch(newscr, y, x)
          : mvwinch(widget->root->c_window, y, x));
      mvwaddch(widget->c_win_shadow, j, i, CTK_WIDGET_SHADOW_CHAR | attr);
    }
}

CTKPRIVATE CTKBOOL
ctk_widget_assert_shadow(ctk_widget_t *widget)
{
  if (widget->c_win_shadow == NULL) {
    if (widget->root == NULL)
      widget->c_win_shadow = newwin(
          widget->height,
          widget->width,
          widget->y + CTK_WIDGET_SHADOW_DY,
          widget->x + CTK_WIDGET_SHADOW_DX);
    else
      widget->c_win_shadow = derwin(
          widget->root->c_window,
          widget->height,
          widget->width,
          widget->y + CTK_WIDGET_SHADOW_DY,
          widget->x + CTK_WIDGET_SHADOW_DX);

    ctk_widget_fill_shadow(widget);
  }

  if (widget->c_win_shadow == NULL)
    return CTK_FALSE;

  if (widget->root == NULL && widget->c_pan_shadow == NULL) {
    if ((widget->c_pan_shadow = new_panel(widget->c_win_shadow)) == NULL)
      return CTK_FALSE;

    if (!widget->visible)
      hide_panel(widget->c_pan_shadow);
  }

  return CTK_TRUE;
}

void
ctk_widget_redraw(ctk_widget_t *widget)
{
  if (widget->visible || widget->root == NULL) {

    if (widget->shadow)
      ctk_widget_fill_shadow(widget);

    wattrset(widget->c_window, widget->attrs);
    wbkgd(widget->c_window, widget->attrs);
    werase(widget->c_window);

    if (widget->has_border)
      box(widget->c_window, 0, 0);

    if (widget->handlers.redraw_handler != NULL) {
      (widget->handlers.redraw_handler) (widget);

      /*
       * Window contents may have changed, the root window must be
       * marked as dirty to indicate that its contents must be
       * flushed to the screen
       */
      if (widget->root != NULL)
        touchwin(widget->root->c_window);
    }
  }
}

CTKBOOL
ctk_widget_set_border(ctk_widget_t *widget, CTKBOOL val)
{
  if (!widget->has_border && val) {
    box(widget->c_window, 0, 0);
    widget->has_border = CTK_TRUE;
  } else if (widget->has_border && !val) {
    /* This is why ncurses sucks */
    wborder(widget->c_window, ' ', ' ', ' ',' ',' ',' ',' ',' ');
    widget->has_border = CTK_FALSE;
  }

  return CTK_TRUE;
}

CTKBOOL
ctk_widget_resize(ctk_widget_t *widget, unsigned int width, unsigned int height)
{
  if (widget->height != height || widget->width != width) {
    /* Border must be erased and redrawn if window is resized */
    if (widget->has_border)
      wborder(widget->c_window, ' ', ' ', ' ',' ',' ',' ',' ',' ');

    /* Update window size and panel */
    if (wresize(widget->c_window, height, width) == ERR)
      return CTK_FALSE;

    /* Update shadow window, if any */
    if (widget->c_win_shadow != NULL)
      if (wresize(widget->c_win_shadow, height, width) == ERR)
        return CTK_FALSE;

    /* Send resize signal to derived widget */
    if (widget->handlers.resize_handler != NULL)
      if (!(widget->handlers.resize_handler)(widget, width, height))
        return CTK_FALSE;

    if (widget->c_panel != NULL) {
      /* Update panel window */
      if (replace_panel(widget->c_panel, widget->c_window) == ERR)
        return CTK_FALSE;

      if (widget->c_pan_shadow != NULL)
        if (replace_panel(widget->c_pan_shadow, widget->c_win_shadow) == ERR)
          return CTK_FALSE;

      /* Refresh virtual screen */
      update_panels();
    }

    widget->height = height;
    widget->width  = width;

    /* Redraw everything */
    ctk_widget_redraw(widget);
  }

  return CTK_TRUE;
}

CTKBOOL
ctk_widget_move(ctk_widget_t *widget, unsigned int x, unsigned int y)
{
  if (widget->x != x || widget->y != y) {
    /* Send move signal to derived widget */
    if (widget->handlers.move_handler != NULL)
      if (!(widget->handlers.move_handler)(widget, x, y))
        return CTK_FALSE;

    if (widget->c_panel != NULL) {
      if (move_panel(widget->c_panel, y, x) == ERR)
        return CTK_FALSE;

      /* No root window, move shadow panel */
      if (widget->c_pan_shadow != NULL)
        if (move_panel(
            widget->c_pan_shadow,
            y + CTK_WIDGET_SHADOW_DY,
            x + CTK_WIDGET_SHADOW_DX) == ERR)
          return CTK_FALSE;
    } else {
      if (mvderwin(widget->c_window, y, x) == ERR)
        return CTK_FALSE;

      /* Move shadow window */
      if (widget->c_win_shadow != NULL)
        if (mvderwin(
            widget->c_win_shadow,
            y + CTK_WIDGET_SHADOW_DY,
            x + CTK_WIDGET_SHADOW_DX) == ERR)
          return CTK_FALSE;
    }

    /* Refresh virtual screen */
    update_panels();

    widget->x = x;
    widget->y = y;
  }

  return CTK_TRUE;
}

CTKBOOL
ctk_widget_show(ctk_widget_t *widget)
{
  if (!widget->visible) {
    if (widget->root == NULL) {
      /* Also show panel shadow */
      if (widget->shadow) {
        ctk_widget_fill_shadow(widget);
        if (show_panel(widget->c_pan_shadow) == ERR)
          return CTK_FALSE;
      }

      if (show_panel(widget->c_panel) == ERR)
        return CTK_FALSE;

      widget->visible = CTK_TRUE;
    } else {
      widget->visible = CTK_TRUE;

      ctk_widget_redraw(widget);
    }

    update_panels();
  }

  return CTK_TRUE;
}

CTKBOOL
ctk_widget_hide(ctk_widget_t *widget)
{
  if (widget->visible) {
    if (widget->root == NULL) {
      /* Also hide panel shadow */
      if (widget->shadow)
        if (hide_panel(widget->c_pan_shadow) == ERR)
          return CTK_FALSE;

      if (hide_panel(widget->c_panel) == ERR)
        return CTK_FALSE;
    } else {
      if (widget->shadow)
        if (werase(widget->c_win_shadow) == ERR)
          return CTK_FALSE;
      if (werase(widget->c_window) == ERR)
        return CTK_FALSE;
    }

    update_panels();

    widget->visible = CTK_FALSE;
  }

  return CTK_TRUE;
}

void
ctk_widget_refresh(ctk_widget_t *widget)
{
  if (widget->visible)
    update_panels();
}

void
ctk_widget_notify_kbd(ctk_widget_t *widget, int c)
{
  if (c == KEY_RESIZE) {
    if (widget->handlers.winch_handler != NULL)
      (widget->handlers.winch_handler) (widget, COLS, LINES);
  } else if (c == CTK_CTRL('L')) {
    ctk_widget_redraw(widget);
  } else {
    if (widget->handlers.kbd_handler != NULL)
      (widget->handlers.kbd_handler) (widget, c);
  }
}

void
ctk_widget_set_popup(ctk_widget_t *widget, CTKBOOL val)
{
  widget->popup = val;
}

void
ctk_widget_set_private(ctk_widget_t *widget, void *ptr)
{
  widget->private = ptr;
}

void *
ctk_widget_get_private(const ctk_widget_t *widget)
{
  return widget->private;
}

void
ctk_widget_get_handlers(
    ctk_widget_t *widget,
    struct ctk_widget_handlers *h)
{
  *h = widget->handlers;
}

void
ctk_widget_set_handlers(
    ctk_widget_t *widget,
    const struct ctk_widget_handlers *h)
{
  widget->handlers = *h;
}

void
ctk_widget_set_attrs(ctk_widget_t *widget, int attrs)
{
  widget->attrs = attrs;
  ctk_widget_redraw(widget);
}

CTKBOOL
ctk_widget_center(ctk_widget_t *widget)
{
  unsigned int x, y;

  if (widget->root == NULL) {
    x = (COLS >> 1) - (widget->width >> 1);
    y = (LINES >> 1) - (widget->height >> 1);
  } else {
    x = (widget->root->width >> 1) - (widget->width >> 1);
    y = (widget->root->height >> 1) - (widget->height >> 1);
  }

  return ctk_widget_move(widget, x, y);
}

void
ctk_widget_destroy(ctk_widget_t *wid)
{
  /* Notify root about destruction of this object */
  if (wid->root != NULL)
    ctk_widget_detach(wid->root, wid);

  if (wid->handlers.dtor_handler != NULL)
    (wid->handlers.dtor_handler) (wid);

  if (wid->c_panel != NULL)
    del_panel(wid->c_panel);

  if (wid->c_window != NULL)
    delwin(wid->c_window);

  if (wid->c_pan_shadow != NULL)
    del_panel(wid->c_pan_shadow);

  if (wid->c_win_shadow != NULL)
    delwin(wid->c_win_shadow);

  free(wid);
}

CTKBOOL
ctk_widget_ctor_end(ctk_widget_t *widget)
{
  /*
   * Object is completely constructed. Now we ask the root widget whether
   * it's ok to attach it.
   */
  if (widget->root != NULL) {
    if (!ctk_widget_attach(widget->root, widget)) {
      fprintf(stderr, "%s: attach failed\n", __FUNCTION__);
      return CTK_FALSE;
    }
  } else
    ctk_widget_redraw(widget);

  return CTK_TRUE;
}

CTKBOOL
ctk_widget_set_shadow(ctk_widget_t *widget, CTKBOOL val)
{
  if (widget->shadow == val)
    return CTK_TRUE;

  if (val && !ctk_widget_assert_shadow(widget))
    return CTK_FALSE;

  widget->shadow = val;

  if (val && widget->visible) {
    if (widget->root == NULL) {
      /*
       * Since the NCurses API is crippled and doesn't let me insert a panel
       * in a given position of the panel stack, I have to show the shadow
       * and then raise the widget panel to the top position. Crazy.
       */
      show_panel(widget->c_pan_shadow);
      top_panel(widget->c_panel);
    } else {
      ctk_widget_redraw(widget);
    }
  }

  return CTK_TRUE;
}

ctk_widget_t *
ctk_widget_ctor_start(
    ctk_widget_t *root,
    unsigned int x,
    unsigned int y,
    unsigned int width,
    unsigned int height,
    size_t alloc_size)
{
  ctk_widget_t *wid = NULL;
  assert(alloc_size >= sizeof(ctk_widget_t));

  if ((wid = calloc(1, alloc_size)) == NULL) {
    fprintf(stderr, "%s: failed to allocate memory\n", __FUNCTION__);
    goto fail;
  }

  wid->class = CTK_WIDGET_CLASS_NONE;
  wid->x = x;
  wid->y = y;
  wid->width = width;
  wid->height = height;
  wid->attrs = COLOR_PAIR(1);

  if (root == NULL)
    wid->c_window = newwin(height, width, y, x);
  else
    wid->c_window = derwin(root->c_window, height, width, y, x);

  if (wid->c_window == NULL) {
    fprintf(
        stderr,
        "%s: failed to create window (%d, %d, %d, %d)\n",
        __FUNCTION__, x, y, width, height);
    goto fail;
  }

  /* Top-level windows are handled through panels */
  if (root == NULL) {
    if ((wid->c_panel = new_panel(wid->c_window)) == NULL) {
      fprintf(stderr, "%s: failed to create panel\n", __FUNCTION__);
      goto fail;
    }

    hide_panel(wid->c_panel);
  }

  if (keypad(wid->c_window, TRUE) == ERR) {
    fprintf(stderr, "%s: failed to enable keypad\n", __FUNCTION__);
    goto fail;
  }

  wid->root = root;

  return wid;

fail:
  if (wid != NULL)
    ctk_widget_destroy(wid);

  return NULL;
}

CTKBOOL
ctk_init(void)
{
  if (initscr() == NULL)
    return CTK_FALSE;

  if (start_color() == ERR)
    return CTK_FALSE;

  if (cbreak() == ERR)
    return CTK_FALSE;

  if (noecho() == ERR)
    return CTK_FALSE;

  if (keypad(stdscr, TRUE) == ERR)
    return CTK_FALSE;

  /* Normal text */
  if (init_pair(CTK_CP_TEXTAREA, COLOR_WHITE, COLOR_BLACK) == ERR)
    return CTK_FALSE;

  /* Menubar text */
  if (init_pair(CTK_CP_WIDGET, COLOR_BLACK, COLOR_WHITE) == ERR)
    return CTK_FALSE;

  /* Selected menu in menubar */
  if (init_pair(CTK_CP_MENU_SELECT, COLOR_WHITE, COLOR_BLUE) == ERR)
    return CTK_FALSE;

  /* Accelerator highlight */
  if (init_pair(CTK_CP_ACCEL_HIGHLIGHT, COLOR_BLUE, COLOR_WHITE) == ERR)
      return CTK_FALSE;

  /* Menu title highlight */
  if (init_pair(CTK_CP_MENU_TITLE_HIGHLIGHT, COLOR_BLACK, COLOR_WHITE) == ERR)
      return CTK_FALSE;

  /* Dialog colors */
  if (init_pair(CTK_CP_DIALOG_NORMAL, COLOR_BLACK, COLOR_WHITE) == ERR)
    return CTK_FALSE;

  if (init_pair(CTK_CP_DIALOG_INFO, COLOR_WHITE, COLOR_BLUE) == ERR)
    return CTK_FALSE;

  if (init_pair(CTK_CP_DIALOG_WARNING, COLOR_BLACK, COLOR_YELLOW) == ERR)
    return CTK_FALSE;

  if (init_pair(CTK_CP_DIALOG_ERROR, COLOR_WHITE, COLOR_RED) == ERR)
    return CTK_FALSE;

  if (init_pair(CTK_CP_BACKGROUND, COLOR_BLACK, COLOR_BLUE) == ERR)
    return CTK_FALSE;

  if (init_pair(CTK_CP_BACKGROUND_TEXT, COLOR_WHITE, COLOR_BLUE) == ERR)
    return CTK_FALSE;

  if (curs_set(0) == ERR)
    return CTK_FALSE;

  wbkgd(stdscr, COLOR_PAIR(CTK_CP_BACKGROUND));
  wclear(stdscr);

  return CTK_TRUE;
}

void
ctk_update(void)
{
  update_panels();
  doupdate();
}

