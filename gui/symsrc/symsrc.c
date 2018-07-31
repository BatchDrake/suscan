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

#define SU_LOG_DOMAIN "symsrc"

#include "gui.h"
#include "symsrc.h"

/************************* Symbol source operations **************************/
SUBITS *
suscan_gui_symsrc_assert(suscan_gui_symsrc_t *symsrc, SUSCOUNT len)
{
  SUBITS *new;

  if (len > symsrc->curr_dec_alloc) {
    SU_TRYCATCH(
        new = realloc(symsrc->curr_dec_buf, len * sizeof(SUBITS)),
        return NULL);

    symsrc->curr_dec_buf = new;
    symsrc->curr_dec_alloc = len;
  }

  symsrc->curr_dec_len = len;

  return symsrc->curr_dec_buf;
}

SUBOOL
suscan_gui_symsrc_commit(suscan_gui_symsrc_t *symsrc)
{
  SU_TRYCATCH(
      suscan_symbuf_append(
          symsrc->symbuf,
          symsrc->curr_dec_buf,
          symsrc->curr_dec_len),
      return SU_FALSE);

  return SU_TRUE;
}

/********************** Codec config UI madness ******************************/

/*
 * Has to be done in a lazy way because when a codec is constructed the
 * parent inspector is detached from the main GUI
 */
SUBOOL
suscan_gui_codec_cfg_ui_assert_parent_gui(struct suscan_gui_codec_cfg_ui *ui)
{
  GtkDialogFlags flags = GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT;
  GtkWidget *content;
  GtkWidget *root;

  if (ui->dialog == NULL) {
    if (ui->symsrc->gui == NULL)
      return SU_FALSE;

    ui->dialog = gtk_dialog_new_with_buttons(
        ui->desc->desc,
        ui->symsrc->gui->main,
        flags,
        "_OK",
        GTK_RESPONSE_ACCEPT,
        "_Cancel",
        GTK_RESPONSE_REJECT,
        NULL);

    content = gtk_dialog_get_content_area(GTK_DIALOG(ui->dialog));
    root = suscan_gui_cfgui_get_root(ui->ui);

    gtk_widget_set_margin_start(root, 20);
    gtk_widget_set_margin_end(root, 20);
    gtk_widget_set_margin_top(root, 20);
    gtk_widget_set_margin_bottom(root, 20);

    gtk_container_add(GTK_CONTAINER(content), root);

    gtk_widget_show(root);
  }

  return SU_TRUE;
}

struct suscan_gui_codec_cfg_ui *
suscan_gui_codec_cfg_ui_new(
    suscan_gui_symsrc_t *symsrc,
    const struct suscan_codec_class *desc)
{
  struct suscan_gui_codec_cfg_ui *new = NULL;


  SU_TRYCATCH(new = calloc(1, sizeof (struct suscan_gui_codec_cfg_ui)), goto fail);

  SU_TRYCATCH(new->config = suscan_codec_class_make_config(desc), goto fail);

  SU_TRYCATCH(new->ui = suscan_gui_cfgui_new(new->config), goto fail);

  new->symsrc = symsrc;
  new->desc = desc;

  return new;

fail:
  if (new != NULL)
    suscan_gui_codec_cfg_ui_destroy(new);

  return NULL;
}

SUBOOL
suscan_gui_codec_cfg_ui_run(struct suscan_gui_codec_cfg_ui *ui)
{
  SUBOOL do_run = SU_FALSE;
  gint response;

  if (ui->ui->widget_count > 0) {
    gtk_dialog_set_default_response(
        GTK_DIALOG(ui->dialog),
        GTK_RESPONSE_ACCEPT);

    do {
      response = gtk_dialog_run(GTK_DIALOG(ui->dialog));

      if (response == GTK_RESPONSE_ACCEPT) {
        if (!suscan_gui_cfgui_parse(ui->ui)) {
          suscan_error(
              ui->symsrc->gui,
              "Encoder/codec parameters",
              "Some parameters are incorrect. Please verify that all mandatory "
              "fields have been properly filled and are within a valid range");
        } else {
          do_run = SU_TRUE;
          break;
        }
      }
    } while (response == GTK_RESPONSE_ACCEPT);

    gtk_widget_hide(ui->dialog);
  } else {
    /* No parameters, create alwats */
    do_run = SU_TRUE;
  }

  return do_run;
}

void
suscan_gui_codec_cfg_ui_destroy(struct suscan_gui_codec_cfg_ui *ui)
{
  /*
   * We don't need to free ui->dialog: is attached to gui->main
   * and it will be disposed automatically on close
   */
  if (ui->config != NULL)
    suscan_config_destroy(ui->config);

  if (ui->ui != NULL)
    suscan_gui_cfgui_destroy(ui->ui);

  free(ui);
}

SUBOOL
suscan_gui_symsrc_register_codec(
    suscan_gui_symsrc_t *this,
    suscan_gui_codec_t *codec)
{
  SU_TRYCATCH(
      (codec->index = PTR_LIST_APPEND_CHECK(this->codec, codec)) != -1,
      return SU_FALSE);

  return SU_TRUE;
}

SUBOOL
suscan_gui_symsrc_unregister_codec(
    suscan_gui_symsrc_t *this,
    suscan_gui_codec_t *codec)
{
  int index = codec->index;

  if (index < 0 || index >= this->codec_count)
    return SU_FALSE;

  SU_TRYCATCH(this->codec_list[index] == codec, return SU_FALSE);

  this->codec_list[index] = NULL;

  return SU_TRUE;
}


SUBOOL
suscan_gui_symsrc_push_task(
    suscan_gui_symsrc_t *symsrc,
    SUBOOL (*task) (
        struct suscan_mq *mq_out,
        void *wk_private,
        void *cb_private),
     void *private)
{
  return suscan_worker_push(symsrc->worker, task, private);
}

SUBOOL
suscan_gui_symsrc_populate_codec_menu(
    suscan_gui_symsrc_t *symsrc,
    SuGtkSymView *view,
    void *(*create_priv) (void *, struct suscan_gui_codec_cfg_ui *),
    void *private,
    GCallback on_encode,
    GCallback on_decode)
{
  GtkWidget *encs, *decs, *item;
  GtkWidget *enc_menu;
  GtkWidget *dec_menu;
  GtkMenu *menu;
  const struct suscan_codec_class **list;
  struct suscan_gui_codec_cfg_ui *ui, *new_ui = NULL;
  unsigned int count;
  unsigned int i;

  menu = sugtk_sym_view_get_menu(view);

  enc_menu = gtk_menu_new();
  dec_menu = gtk_menu_new();

  encs = gtk_menu_item_new_with_label("Encode with...");
  decs = gtk_menu_item_new_with_label("Decode with...");

  gtk_menu_item_set_submenu(GTK_MENU_ITEM(encs), enc_menu);
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(decs), dec_menu);

  gtk_menu_shell_append(GTK_MENU_SHELL(menu), encs);
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), decs);

  /* Append all available codecs */
  suscan_codec_class_get_list(&list, &count);
  for (i = 0; i < count; ++i) {
    /*
     * We will ASSERT this UI, instead of re-creating it
     * for every SymbolView
     */
    if (i < symsrc->codec_cfg_ui_count) {
      ui = symsrc->codec_cfg_ui_list[i];
    } else {
      SU_TRYCATCH(
          new_ui = suscan_gui_codec_cfg_ui_new(symsrc, list[i]),
          return SU_FALSE);

      SU_TRYCATCH(
          PTR_LIST_APPEND_CHECK(symsrc->codec_cfg_ui, new_ui) != -1,
          goto fail);

      ui = new_ui;
      new_ui = NULL;
    }

    /* To be handled by the encoder */
    if (list[i]->directions & SUSCAN_CODEC_DIRECTION_FORWARDS) {
      item = gtk_menu_item_new_with_label(list[i]->desc);
      gtk_menu_shell_append(GTK_MENU_SHELL(enc_menu), item);
      g_signal_connect(
          G_OBJECT(item),
          "activate",
          on_encode,
          (create_priv) (private, ui));
    }

    /* To be handled by the decoder */
    if (list[i]->directions & SUSCAN_CODEC_DIRECTION_BACKWARDS) {
      item = gtk_menu_item_new_with_label(list[i]->desc);
      gtk_menu_shell_append(GTK_MENU_SHELL(dec_menu), item);
      g_signal_connect(
          G_OBJECT(item),
          "activate",
          on_decode,
          (create_priv) (private, ui));
    }
  }

  /* Show everything */
  gtk_widget_show_all(GTK_WIDGET(menu));

  return SU_TRUE;

fail:
  if (new_ui != NULL)
    suscan_gui_codec_cfg_ui_destroy(new_ui);

  return SU_FALSE;
}

SUBOOL
suscan_gui_symsrc_init(suscan_gui_symsrc_t *this, suscan_gui_t *gui)
{
  memset(this, 0, sizeof (struct suscan_gui_symsrc));

  SU_TRYCATCH(suscan_mq_init(&this->mq), goto fail);
  SU_TRYCATCH(this->worker = suscan_worker_new(&this->mq, this), goto fail);

  SU_TRYCATCH(this->symbuf = suscan_symbuf_new(), goto fail);

  this->gui = gui;

  return SU_TRUE;

fail:
  suscan_gui_symsrc_finalize(this);

  return SU_FALSE;
}

SUBOOL
suscan_gui_symsrc_finalize(suscan_gui_symsrc_t *this)
{
  unsigned int i;

  if (this->worker != NULL)
    if (!suscan_worker_halt(this->worker)) {
      SU_ERROR("Symsrc worker destruction failed, memory leak ahead\n");
      return SU_FALSE;
    }

  if (this->symbuf != NULL)
    suscan_symbuf_destroy(this->symbuf);

  if (this->curr_dec_buf != NULL)
    free(this->curr_dec_buf);

  for (i = 0; i < this->codec_count; ++i)
    if (this->codec_list[i] != NULL)
      suscan_gui_codec_destroy_hard(this->codec_list[i]);

  if (this->codec_list != NULL)
    free(this->codec_list);

  for (i = 0; i < this->codec_cfg_ui_count; ++i)
    suscan_gui_codec_cfg_ui_destroy(this->codec_cfg_ui_list[i]);

  if (this->codec_cfg_ui_list != NULL)
    free(this->codec_cfg_ui_list);
  return SU_TRUE;
}

