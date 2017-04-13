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

#ifndef _CTK_H
#define _CTK_H

#include <ncurses.h>
#include <menu.h>
#include <panel.h>
#include <ctype.h>
#include <util.h>

#define CTKPRIVATE  static
#define CTKBOOL     int
#define CTK_TRUE    1
#define CTK_FALSE   0

#define CTK_KEY_ESCAPE 033

#define CTK_CLASS_INHERITABLE char __sub[0]
#define CTK_OBJECT_GET_SUB(x) ((x)->__sub)

#define CTK_WIDGET(x) ((ctk_widget_t *) (x))
#define CTK_WIDGET_SUB_ALLOC_SIZE(x) (sizeof(ctk_widget_t) + sizeof(x))
#define CTK_WIDGET_ASSERT_CLASS(x, t) \
  assert(CTK_WIDGET(x)->class == t)

#define CTK_ITEM_OVERFLOW_STRING "[...]"

#define CTK_WIDGET_SHADOW_DX 2
#define CTK_WIDGET_SHADOW_DY 1
#define CTK_WIDGET_SHADOW_CHAR ACS_CKBOARD

#define CTK_CTRL(key) (toupper(key) - '@')

/* Odd colors are shaded colors */
#define CTK_CP_PURE_BLACK           1
#define CTK_CP_TEXTAREA             2
#define CTK_CP_WIDGET               4
#define CTK_CP_MENU_SELECT          6
#define CTK_CP_ACCEL_HIGHLIGHT      8
#define CTK_CP_MENU_TITLE_HIGHLIGHT 10
#define CTK_CP_DIALOG_COLOR_START   12
#define CTK_CP_DIALOG_NORMAL        12
#define CTK_CP_DIALOG_INFO          14
#define CTK_CP_DIALOG_WARNING       16
#define CTK_CP_DIALOG_ERROR         18
#define CTK_CP_BACKGROUND           20
#define CTK_CP_BACKGROUND_TEXT      22

#define CTK_ITEM_INDEX(item) (item)->__index

/* Button defines */
#define CTK_BUTTON_MIN_SIZE         10

/* Message box dialog defines */
#define CTK_DIALOG_MSGBOX_MAX_WIDTH          (COLS - 8)
#define CTK_DIALOG_MSGBOX_MIN_WIDTH          15
#define CTK_DIALOG_MSGBOX_HORIZONTAL_PADDING 4
#define CTK_DIALOG_MSGBOX_VERTICAL_PADDING   6

/* File dialog layout defines */
#define CTK_DIALOG_FILE_CHOOSER_WIDTH   60
#define CTK_DIALOG_FILE_CHOOSER_HEIGHT  22
#define CTK_DIALOG_FILE_PATH_X          2
#define CTK_DIALOG_FILE_PATH_Y          2
#define CTK_DIALOG_FILE_DIR_X           2
#define CTK_DIALOG_FILE_DIR_Y           4
#define CTK_DIALOG_FILE_DIR_WIDTH       20
#define CTK_DIALOG_FILE_DIR_HEIGHT      (CTK_DIALOG_FILE_CHOOSER_HEIGHT - CTK_DIALOG_FILE_DIR_Y - 2)
#define CTK_DIALOG_FILE_FILENAME_X      (CTK_DIALOG_FILE_DIR_WIDTH + CTK_DIALOG_FILE_DIR_X)
#define CTK_DIALOG_FILE_FILENAME_Y      4
#define CTK_DIALOG_FILE_FILENAME_WIDTH  (CTK_DIALOG_FILE_CHOOSER_WIDTH - CTK_DIALOG_FILE_DIR_WIDTH - 4)
#define CTK_DIALOG_FILE_FILENAME_HEIGHT (CTK_DIALOG_FILE_CHOOSER_HEIGHT - CTK_DIALOG_FILE_FILENAME_Y - 2)

#define CTK_DIALOG_FILE_CANCEL_BUTTON_X (CTK_DIALOG_FILE_CHOOSER_WIDTH - 26)
#define CTK_DIALOG_FILE_CANCEL_BUTTON_Y (CTK_DIALOG_FILE_CHOOSER_HEIGHT - 2)

#define CTK_DIALOG_FILE_OK_BUTTON_X     (CTK_DIALOG_FILE_CHOOSER_WIDTH - 14)
#define CTK_DIALOG_FILE_OK_BUTTON_Y     (CTK_DIALOG_FILE_CHOOSER_HEIGHT - 2)

struct ctk_item {
  char *name;
  char *desc;
  void *private;

  /* Private members */
  int __index;
  char *__printable_name; /* Yes, NCurses is that dumb */
};

enum ctk_widget_class {
  CTK_WIDGET_CLASS_NONE,
  CTK_WIDGET_CLASS_WINDOW,
  CTK_WIDGET_CLASS_ENTRY,
  CTK_WIDGET_CLASS_MENU,
  CTK_WIDGET_CLASS_MENUBAR,
  CTK_WIDGET_CLASS_BUTTON,
};

enum ctk_dialog_kind {
  CTK_DIALOG_NORMAL,
  CTK_DIALOG_INFO,
  CTK_DIALOG_WARNING,
  CTK_DIALOG_ERROR
};

enum ctk_dialog_response {
  CTK_DIALOG_RESPONSE_ERROR  = -1,
  CTK_DIALOG_RESPONSE_OK     = 0,
  CTK_DIALOG_RESPONSE_CANCEL = 1,
  CTK_DIALOG_RESPONSE_YES    = 2,
  CTK_DIALOG_RESPONSE_NO     = 3,
};

struct ctk_widget;

typedef void    (*ctk_kbd_handler_t) (struct ctk_widget *, int);
typedef void    (*ctk_dtor_handler_t) (struct ctk_widget *);
typedef CTKBOOL (*ctk_resize_handler_t) (struct ctk_widget *, unsigned int, unsigned int);
typedef CTKBOOL (*ctk_move_handler_t) (struct ctk_widget *, unsigned int, unsigned int);
typedef void    (*ctk_winch_handler_t) (struct ctk_widget *, unsigned int, unsigned int);
typedef void    (*ctk_submit_handler_t) (struct ctk_widget *, struct ctk_item *);
typedef CTKBOOL (*ctk_attach_handler_t) (struct ctk_widget *, struct ctk_widget *);
typedef void    (*ctk_detach_handler_t) (struct ctk_widget *, struct ctk_widget *);
typedef void    (*ctk_focus_handler_t) (struct ctk_widget *);
typedef void    (*ctk_blur_handler_t) (struct ctk_widget *);
typedef void    (*ctk_redraw_handler_t) (struct ctk_widget *);

struct ctk_widget_handlers {
  ctk_kbd_handler_t    kbd_handler;
  ctk_dtor_handler_t   dtor_handler;
  ctk_resize_handler_t resize_handler;
  ctk_move_handler_t   move_handler;
  ctk_winch_handler_t  winch_handler;
  ctk_submit_handler_t submit_handler;
  ctk_attach_handler_t attach_handler;
  ctk_detach_handler_t detach_handler;
  ctk_focus_handler_t  focus_handler;
  ctk_blur_handler_t   blur_handler;
  ctk_redraw_handler_t redraw_handler;
};

/* Base class for all ctk_widgets */
struct ctk_widget {
  enum ctk_widget_class class;
  struct ctk_widget *root;
  unsigned int x;
  unsigned int y;
  unsigned int width;
  unsigned int height;

  int attrs;
  int accel;

  CTKBOOL has_border;
  CTKBOOL popup;
  CTKBOOL visible;
  CTKBOOL shadow;

  /* Curses objects */
  void *private;
  WINDOW *c_window;
  PANEL  *c_panel;

  WINDOW *c_win_shadow;
  PANEL  *c_pan_shadow;

  /* Event handlers */
  struct ctk_widget_handlers handlers;

  CTK_CLASS_INHERITABLE;
};

typedef struct ctk_widget ctk_widget_t;


/********************* CTK Menu definitions **********************************/
#define CTK_WIDGET_AS_MENU(x) ((ctk_menu_t *) CTK_OBJECT_GET_SUB(x))

struct ctk_menu {
  PTR_LIST(struct ctk_item, item); /* No gaps allowed! */
  PTR_LIST(struct ctk_item, old_item); /* Old item list */
  char   *title;
  CTKBOOL autoresize;
  CTKBOOL has_focus;

  /* Curses objects */
  ITEM  **c_item_list;
  WINDOW *c_sub;
  MENU   *c_menu;
};

typedef struct ctk_menu ctk_menu_t;

/********************** CTK Menu bar definitions *****************************/
#define CTK_WIDGET_AS_MENUBAR(x) ((ctk_menubar_t *) CTK_OBJECT_GET_SUB(x))

struct ctk_menubar {
  PTR_LIST(ctk_widget_t, menu);
  CTKBOOL escape;
  int active; /* Active menu inex */
};

typedef struct ctk_menubar ctk_menubar_t;

/*********************** CTK Window definitions ******************************/
#define CTK_WIDGET_AS_WINDOW(x) ((ctk_window_t *) CTK_OBJECT_GET_SUB(x))

struct ctk_window {
  PTR_LIST(ctk_widget_t, widget);
  char *title;
  int focus; /* Focused element */
};

typedef struct ctk_window ctk_window_t;

/*********************** CTK Button definitions ******************************/
#define CTK_WIDGET_AS_BUTTON(x) ((ctk_button_t *) CTK_OBJECT_GET_SUB(x))

struct ctk_button {
  char *caption;
  CTKBOOL has_focus;
};

typedef struct ctk_button ctk_button_t;

/************************ CTK Entry definitions ******************************/
#define CTK_WIDGET_AS_ENTRY(x) ((ctk_entry_t *) CTK_OBJECT_GET_SUB(x))

typedef CTKBOOL (*ctk_entry_validator_t) (const char *string, char c, int p);

struct ctk_entry {
  char *buffer;
  unsigned int allocation;
  unsigned int length; /* Not counting NUL byte */
  unsigned int p; /* Insert position */
  unsigned int pos; /* Display position */
  int cur_attr; /* Cursor attribute */
  CTKBOOL has_focus;

  ctk_entry_validator_t validator; /* Content validator */
};

typedef struct ctk_entry ctk_entry_t;

/*
 * Validators are not a good way to ensure the input has a valid format,
 * they are intended to be a guide for the user to input correct values.
 */
void ctk_entry_set_validator(
    const ctk_widget_t *widget,
    ctk_entry_validator_t cb);

/*
 * Builtin validators
 */
CTKBOOL ctk_entry_uint32_validator(const char *string, char c, int p);
CTKBOOL ctk_entry_uint64_validator(const char *string, char c, int p);
CTKBOOL ctk_entry_float_validator(const char *string, char c, int p);

/***************************** CTK Widget API ********************************/
void ctk_widget_destroy(ctk_widget_t *wid);
ctk_widget_t *ctk_widget_ctor_start(
    ctk_widget_t *root,
    unsigned int x,
    unsigned int y,
    unsigned int width,
    unsigned int height,
    size_t alloc_size);
CTKBOOL ctk_widget_ctor_end(ctk_widget_t *widget);

CTKBOOL ctk_widget_set_border(ctk_widget_t *widget, CTKBOOL val);
CTKBOOL ctk_widget_resize(
    ctk_widget_t *widget,
    unsigned int width,
    unsigned int height);
CTKBOOL ctk_widget_move(ctk_widget_t *widget, unsigned int x, unsigned int y);
CTKBOOL ctk_widget_center(ctk_widget_t *widget);
CTKBOOL ctk_widget_set_shadow(ctk_widget_t *widget, CTKBOOL val);
CTKBOOL ctk_widget_show(ctk_widget_t *widget);
CTKBOOL ctk_widget_hide(ctk_widget_t *widget);
void ctk_widget_focus(ctk_widget_t *widget);
void ctk_widget_blur(ctk_widget_t *widget);
void ctk_widget_refresh(ctk_widget_t *widget);
void ctk_widget_submit(ctk_widget_t *widget, struct ctk_item *item);
void ctk_widget_notify_kbd(ctk_widget_t *widget, int c);
void ctk_widget_set_popup(ctk_widget_t *widget, CTKBOOL val);
void ctk_widget_set_private(ctk_widget_t *widget, void *ptr);
void *ctk_widget_get_private(const ctk_widget_t *widget);
void ctk_widget_set_attrs(ctk_widget_t *widget, int attrs);
void ctk_widget_set_accel(ctk_widget_t *widget, int accel);
int  ctk_widget_get_accel(const ctk_widget_t *widget);
void ctk_widget_redraw(ctk_widget_t *widget);
void ctk_widget_get_handlers(
    ctk_widget_t *widget,
    struct ctk_widget_handlers *h);
void ctk_widget_set_handlers(
    ctk_widget_t *widget,
    const struct ctk_widget_handlers *h);

/***************************** CTK Item API **********************************/
struct ctk_item *ctk_item_new(const char *name, const char *desc, void *private);
struct ctk_item *ctk_item_dup(const struct ctk_item *item);
void ctk_item_remove_non_printable(struct ctk_item *item, unsigned int max);
void ctk_item_destroy(struct ctk_item *item);

/**************************** Misc functions *********************************/
int ctk_widget_lookup_index_by_accel(
    ctk_widget_t * const *list,
    unsigned int count,
    int accel);

/************************* CTK Menu functions ********************************/
ctk_widget_t *ctk_menu_new(ctk_widget_t *, unsigned int x, unsigned int y);
CTKBOOL ctk_menu_add_item(
    ctk_widget_t *widget,
    const char *name,
    const char *desc,
    void *private);
CTKBOOL ctk_menu_add_multiple_items(
    ctk_widget_t *widget,
    const struct ctk_item *items,
    unsigned int count);
CTKBOOL __ctk_menu_add_item(
    ctk_widget_t *menu,
    const char *name,
    const char *desc,
    void *private);
CTKBOOL __ctk_menu_update(ctk_widget_t *widget);
CTKBOOL ctk_menu_set_title(ctk_widget_t *widget, const char *title);
const char *ctk_menu_get_title(ctk_widget_t *widget);
unsigned int ctk_menu_get_item_count(const ctk_widget_t *widget);
CTKBOOL ctk_menu_sort(
    ctk_widget_t *widget,
    int (*cmp)(const struct ctk_item *, const struct ctk_item *));
void ctk_menu_on_kbd(ctk_widget_t *widget, int c);
struct ctk_item *ctk_menu_get_first_item(const ctk_widget_t *widget);
struct ctk_item *ctk_menu_get_current_item(const ctk_widget_t *widget);
unsigned int ctk_menu_get_max_item_name_length(const ctk_widget_t *widget);
void ctk_menu_set_autoresize(const ctk_widget_t *widget, CTKBOOL val);

/************************** CTK Menubar functions ****************************/
ctk_widget_t *ctk_menubar_new(void);
CTKBOOL ctk_menubar_add_menu(ctk_widget_t *, const char *, ctk_widget_t *);
void ctk_menu_clear(ctk_widget_t *widget);
int ctk_window_find_new_accel(const ctk_window_t *bar, const char *caption);

/************************* CTK Window functions ******************************/
ctk_widget_t *ctk_window_new(const char *title);
void ctk_window_focus_next(ctk_widget_t *widget);
CTKBOOL ctk_window_set_focus(ctk_widget_t *widget, ctk_widget_t *target);

/************************* CTK Button functions ******************************/
ctk_widget_t *ctk_button_new(
    ctk_widget_t *root,
    unsigned int x,
    unsigned int y,
    const char *caption);
CTKBOOL ctk_button_set_caption(ctk_widget_t *widget, const char *caption);
const char *ctk_button_get_caption(ctk_widget_t *widget);

/************************ CTK Selbutton functions ****************************/
ctk_widget_t *ctk_selbutton_new(
    ctk_widget_t *root,
    unsigned int x,
    unsigned int y,
    ctk_widget_t *menu);
void ctk_selbutton_set_on_submit(ctk_widget_t *widget, ctk_submit_handler_t cb);
void ctk_selbutton_set_current_item(
    ctk_widget_t *widget,
    struct ctk_item *item);
struct ctk_item *ctk_menu_get_item_at(
    const ctk_widget_t *widget,
    unsigned int index);
struct ctk_item *ctk_selbutton_get_current_item(ctk_widget_t *widget);
void ctk_selbutton_set_private(ctk_widget_t *widget, void *private);
void *ctk_selbutton_get_private(const ctk_widget_t *widget);
CTKBOOL ctk_selbutton_adjust_size(ctk_widget_t *widget);

/************************** CTK Entry functions ******************************/
ctk_widget_t *ctk_entry_new(
    ctk_widget_t *root,
    unsigned int x,
    unsigned int y,
    unsigned int width);
const char *ctk_entry_get_text(const ctk_widget_t *widget);
CTKBOOL ctk_entry_set_text(ctk_widget_t *widget, const char *text);
CTKBOOL ctk_entry_set_cursor(ctk_widget_t *widget, unsigned int p);

/************************* CTK Dialog functions ******************************/
CTKBOOL ctk_msgbox(
    enum ctk_dialog_kind kind,
    const char *title,
    const char *msg);
CTKBOOL ctk_msgboxf(
    enum ctk_dialog_kind kind,
    const char *title,
    const char *fmt, ...);

#define ctk_error(title, fmt, arg...) \
  ctk_msgboxf(CTK_DIALOG_ERROR, title, fmt, ##arg)

#define ctk_warning(title, fmt, arg...) \
  ctk_msgboxf(CTK_DIALOG_WARNING, title, fmt, ##arg)

#define ctk_info(title, fmt, arg...) \
  ctk_msgboxf(CTK_DIALOG_INFO, title, fmt, ##arg)

#define ctk_normal(title, fmt, arg...) \
  ctk_msgboxf(CTK_DIALOG_NORMAL, title, fmt, ##arg)

enum ctk_dialog_response ctk_file_dialog(const char *text, char **file);

/************************** Generic CTK methods ******************************/
CTKBOOL ctk_init(void);
void ctk_update(void);
int ctk_getch_async(void);
int ctk_getch(void);

#endif /* _CTK_H */
