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

#ifndef _GUI_SYMSRC_H
#define _GUI_SYMSRC_H

#include <analyzer/symbuf.h>
#include <analyzer/worker.h>

#include "codec.h"

struct suscan_gui;

struct suscan_gui_symsrc {
  struct suscan_gui *gui; /* Parent GUI */

  /* Worker used by codecs */
  suscan_worker_t *worker;
  struct suscan_mq mq;

  /* Symbol buffer */
  suscan_symbuf_t *symbuf;
  SUBITS  *curr_dec_buf;
  SUSCOUNT curr_dec_alloc;
  SUSCOUNT curr_dec_len;

  /* DecoderUI objects */
  PTR_LIST(struct suscan_gui_codec_cfg_ui, codec_cfg_ui);

  /* Decoder objects */
  PTR_LIST(struct suscan_gui_codec, codec);
};

typedef struct suscan_gui_symsrc suscan_gui_symsrc_t;

SUBOOL suscan_gui_symsrc_push_task(
    suscan_gui_symsrc_t *symsrc,
    SUBOOL (*task) (
        struct suscan_mq *mq_out,
        void *wk_private,
        void *cb_private),
     void *private);

SUBITS *suscan_gui_symsrc_assert(
    suscan_gui_symsrc_t *symsrc,
    SUSCOUNT len);

SUBOOL suscan_gui_symsrc_commit(suscan_gui_symsrc_t *symsrc);

SUBOOL
suscan_gui_symsrc_populate_codec_menu(
    suscan_gui_symsrc_t *symsrc,
    SuGtkSymView *view,
    void *(*create_priv) (void *, struct suscan_gui_codec_cfg_ui *),
    void *private,
    GCallback on_encode,
    GCallback on_decode);

SUBOOL suscan_gui_symsrc_register_codec(
    suscan_gui_symsrc_t *this,
    suscan_gui_codec_t *codec);

SUBOOL suscan_gui_symsrc_unregister_codec(
    suscan_gui_symsrc_t *this,
    suscan_gui_codec_t *codec);

SUBOOL suscan_gui_symsrc_init(
    suscan_gui_symsrc_t *this,
    struct suscan_gui *gui);

SUBOOL suscan_gui_symsrc_finalize(suscan_gui_symsrc_t *);

#endif /* _GUI_SYMSRC_H */
