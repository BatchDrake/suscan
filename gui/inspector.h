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

#ifndef _GUI_INSPECTOR_H
#define _GUI_INSPECTOR_H

#include <analyzer/symbuf.h>

#include "constellation.h"
#include "spectrum.h"
#include "codec.h"

#include "transmtx.h"

#define SUSCAN_GUI_INSPECTOR_SPECTRUM_AGC_ALPHA .5
#define SUSCAN_GUI_INSPECTOR_SPECTRUM_MODE SUSCAN_GUI_SPECTRUM_MODE_SPECTROGRAM

struct suscan_gui;

struct suscan_gui_inspector {
  int index; /* Back reference */
  SUHANDLE inshnd; /* Inspector handle (relative to current analyzer) */
  SUBOOL dead; /* Owner analyzer has been destroyed */
  SUBOOL recording; /* Symbol recorder enabled */
  struct suscan_gui *gui; /* Parent GUI */
  struct suscan_gui_constellation constellation; /* Constellation graph */
  struct suscan_gui_spectrum spectrum; /* Spectrum graph */
  struct suscan_inspector_params params; /* Inspector params */

  /* Worker used by codecs */
  suscan_worker_t *worker;
  struct suscan_mq mq;

  /* Symbol buffer */
  suscan_symbuf_t *symbuf;
  SUBITS  *curr_dec_buf;
  SUSCOUNT curr_dec_len;

  /* Widgets */
  GtkBuilder     *builder;
  GtkEventBox    *pageLabelEventBox;
  GtkLabel       *pageLabel;
  GtkGrid        *channelInspectorGrid;
  GtkToggleToolButton *autoScrollToggleButton;
  GtkToggleToolButton *autoFitToggleButton;
  GtkNotebook    *constellationNotebook;
  SuGtkTransMtx  *transMatrix;
  GtkAlignment   *transAlignment;

  /* Gain control widgets */
  GtkRadioButton *automaticGainRadioButton;
  GtkRadioButton *manualGainRadioButton;
  GtkAlignment   *gainManualAlignment;
  GtkEntry       *gainEntry;
  GtkScale       *gainFineTuneScale;

  /* Carrier control alignment */
  GtkRadioButton *costas2RadioButton;
  GtkRadioButton *costas4RadioButton;
  GtkRadioButton *costas8RadioButton;
  GtkRadioButton *manualRadioButton;
  GtkAlignment   *carrierManualAlignment;
  GtkEntry       *carrierOffsetEntry;
  GtkScale       *fineTuneScale;
  GtkScale       *phaseScale;

  /* Clock control widgets */
  GtkRadioButton *clockGardnerRadioButton;
  GtkRadioButton *clockManualRadioButton;
  GtkRadioButton *clockDisableButton;
  GtkAlignment   *clockGardnerAlignment;
  GtkEntry       *gardnerAlphaEntry;
  GtkCheckButton *gardnerEnableBetaCheckButton;
  GtkEntry       *gardnerBetaEntry;
  GtkAlignment   *clockManualAlignment;
  GtkEntry       *baudRateEntry;
  GtkScale       *symbolPhaseScale;
  GtkButton      *setBaudRateButton;
  GtkButton      *detectBaudRateFACButton;
  GtkButton      *detectBaudRateNLNButton;
  GtkScale       *fineBaudScale;

  /* Matched filter widgets */
  GtkRadioButton *matchedFilterBypassRadioButton;
  GtkRadioButton *matchedFilterRRCRadioButton;
  GtkGrid        *rootRaisedCosineGrid;
  GtkScale       *rollOffScale;

  /* Equalizer widgets */
  GtkRadioButton *eqBypassRadioButton;
  GtkRadioButton *eqCMARadioButton;
  GtkGrid        *eqCMAGrid;
  GtkEntry       *eqMuEntry;

  /* Spectrum source widgets */
  GtkRadioButton *powerSpectrumRadioButton;
  GtkRadioButton *cycloSpectrumRadioButton;
  GtkRadioButton *noSpectrumRadioButton;

  /* Symbol recorder widgets */
  GtkGrid        *recorderGrid;
  SuGtkSymView   *symbolView;
  GtkSpinButton  *offsetSpinButton;
  GtkSpinButton  *widthSpinButton;
  GtkNotebook    *codecNotebook;

  /* Progress dialog */
  GtkDialog      *progressDialog;
  GtkProgressBar *progressBar;

  /* DecoderUI objects */
  PTR_LIST(struct suscan_gui_codec_cfg_ui, codec_cfg_ui);

  /* Decoder objects */
  PTR_LIST(struct suscan_gui_codec, codec);

  struct sigutils_channel channel;
};

/* Inspector GUI functions */
SUBOOL suscan_gui_inspector_feed_w_batch(
    struct suscan_gui_inspector *inspector,
    const struct suscan_analyzer_sample_batch_msg *msg);

SUBOOL suscan_gui_inspector_push_task(
    struct suscan_gui_inspector *inspector,
    SUBOOL (*task) (
        struct suscan_mq *mq_out,
        void *wk_private,
        void *cb_private),
     void *private);

struct suscan_gui_inspector *suscan_gui_inspector_new(
    const struct sigutils_channel *channel,
    SUHANDLE handle);

SUBOOL suscan_gui_inspector_update_sensitiveness(
    struct suscan_gui_inspector *insp,
    const struct suscan_inspector_params *params);

void suscan_gui_inspector_detach(struct suscan_gui_inspector *insp);

void suscan_gui_inspector_close(struct suscan_gui_inspector *insp);

SUBOOL suscan_gui_inspector_populate_codec_menu(
    struct suscan_gui_inspector *inspector,
    SuGtkSymView *view,
    void *(*create_priv) (void *, struct suscan_gui_codec_cfg_ui *),
    void *private,
    GCallback on_encode,
    GCallback on_decode);

SUBOOL suscan_gui_inspector_remove_codec(
    struct suscan_gui_inspector *gui,
    struct suscan_gui_codec *codec);

SUBOOL suscan_gui_inspector_add_codec(
    struct suscan_gui_inspector *inspector,
    struct suscan_gui_codec *codec);

SUBOOL suscan_gui_inspector_open_codec_tab(
    struct suscan_gui_inspector *inspector,
    struct suscan_gui_codec_cfg_ui *ui,
    unsigned int bits,
    unsigned int direction,
    const SuGtkSymView *source);

void suscan_gui_inspector_destroy(struct suscan_gui_inspector *inspector);

#endif /* _GUI_INSPECTOR_H */
