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

#define SU_LOG_DOMAIN "codec-gui"

#include "gui.h"
#include <sigutils/agc.h>
#include "codec.h"

/********************* Asynchronous processing *******************************/
/*
 * Processing will happen asynchronously in a worker callback depending on
 * the suscan_gui_codec state (which is protected by mutexes).
 *
 * This callback will not block for a long time (but it *may* block
 * nonetheless), and after every execution it will produce updates to
 * the GUI (using Gtk's async API) with the current decoded bits and progress
 * states.
 *
 * There are two important aspects to have in mind:
 * 1. suscan_gui_codec will be accessible both from worker and the GUI,
 *    therefore it must be protected by mutexes.
 * 2. suscan_gui_codec may be destroyed before the processing is done,
 *    keeping us from releasing the memory used by the worker.
 *
 * This motivates the following design:
 * - We keep a suscan_gui_codec_state, which holds the suscan_codec, the
 *   input & output buffers, pointers, and other non-GUI dependant
 *   parts. This object is protected by mutexes and has a reference counter.
 * - When suscan_gui_codec is destroyed (or the processing is finished)
 *   the reference counter is decremented. When reaches zero, it is
 *   destroyed.
 * - This object is private to this file
 */

#define SUSCAN_GUI_CODEC_MAX_BLOCK_SIZE 4096

enum suscan_gui_codec_state_kind {
  SUSCAN_GUI_CODEC_STATE_BUSY,      /* Processing (initial state) */
  SUSCAN_GUI_CODEC_STATE_CANCELING, /* Canceled by user */
  SUSCAN_GUI_CODEC_STATE_ORPHAN,    /* Owner has been destroyed */
  SUSCAN_GUI_CODEC_STATE_DONE       /* Processing has finished */
};

struct suscan_gui_codec_state {
  pthread_mutex_t mutex;
  unsigned int ref_count;

  /*
   * These members are shared (protected by mutex)
   */
  enum suscan_gui_codec_state_kind state;
  grow_buf_t output; /* Cleared after every dump to SymView */
  struct suscan_codec_progress progress;
  struct suscan_gui_codec *owner;

  /*
   * This members are worker-private and are not protected by the mutex.
   * suscan_gui_codec should not access them directly
   */
  suscan_codec_t *codec;
  grow_buf_t _output; /* Cleared after every dump to output */
  struct suscan_codec_progress _progress; /* Ditto */
  SUBITS *input; /* Input symbols */
  SUSCOUNT input_len; /* Input length */
  SUSCOUNT ptr;  /* Processing pointer */
};

void
suscan_gui_codec_state_destroy(struct suscan_gui_codec_state *state)
{
  /*
   * Ensure it's locked to unlock it and then destroy it. We do it like
   * this to avoid "already-unlocked" errors while debugging application's
   * concurrency.
   */
  (void) pthread_mutex_trylock(&state->mutex);
  (void) pthread_mutex_unlock(&state->mutex);
  (void) pthread_mutex_destroy(&state->mutex);

  if (state->codec != NULL)
    suscan_codec_destroy(state->codec);

  if (state->input != NULL)
    free(state->input);

  if (grow_buf_get_buffer(&state->_output) != NULL)
    grow_buf_finalize(&state->_output);

  if (grow_buf_get_buffer(&state->output) != NULL)
    grow_buf_finalize(&state->output);

  if (state->_progress.message != NULL)
    free(state->_progress.message);

  if (state->progress.message != NULL)
    free(state->progress.message);

  free(state);
}

void
suscan_gui_codec_state_lock(struct suscan_gui_codec_state *state)
{
  (void) pthread_mutex_lock(&state->mutex);
}

void
suscan_gui_codec_state_unlock(struct suscan_gui_codec_state *state)
{
  (void) pthread_mutex_unlock(&state->mutex);
}

void
suscan_gui_codec_state_addref_internal(struct suscan_gui_codec_state *state)
{
  ++state->ref_count;
}

SUBOOL
suscan_gui_codec_state_unref_internal(struct suscan_gui_codec_state *state)
{
  if (--state->ref_count == 0) {
    suscan_gui_codec_state_destroy(state);
    return SU_TRUE;
  }

  return SU_FALSE;
}

/*************************** Idle callbacks **********************************/
SUPRIVATE gboolean
suscan_gui_codec_async_append_data(gpointer user_data)
{
  struct suscan_gui_codec_state *state =
      (struct suscan_gui_codec_state *) user_data;

  unsigned int i, len;
  uint8_t *bytes;
  unsigned int bits_per_sym;

  suscan_gui_codec_state_lock(state);

  if (state->state != SUSCAN_GUI_CODEC_STATE_ORPHAN) {
    bits_per_sym = suscan_codec_get_output_bits_per_symbol(state->codec);

    len = grow_buf_get_size(&state->output);
    bytes = grow_buf_get_buffer(&state->output);

    /* Transfer all bytes from the current output to the symbol view */
    for (i = 0; i < len; ++i)
      sugtk_sym_view_append(
          state->owner->symbolView,
          sugtk_sym_view_code_to_pixel_helper(bits_per_sym, bytes[i]));

    /* Clear output buffer */
    grow_buf_clear(&state->output);
  }

  suscan_gui_codec_state_unlock(state);

  return G_SOURCE_REMOVE;
}

SUPRIVATE gboolean
suscan_gui_codec_async_parse_progress(gpointer user_data)
{
  struct suscan_gui_codec_state *state =
      (struct suscan_gui_codec_state *) user_data;

  suscan_gui_codec_state_lock(state);

  if (state->state != SUSCAN_GUI_CODEC_STATE_ORPHAN) {
    if (state->progress.updated) {
      gtk_widget_show_all(GTK_WIDGET(state->owner->inspector->progressDialog));

      if (state->progress.progress == SUSCAN_CODEC_PROGRESS_UNDEFINED)
        gtk_progress_bar_pulse(state->owner->inspector->progressBar);
      else
        gtk_progress_bar_set_fraction(
            state->owner->inspector->progressBar,
            state->progress.progress);

      if (state->progress.message != NULL)
        gtk_progress_bar_set_text(
            state->owner->inspector->progressBar,
            state->progress.message);
    }
  }

  suscan_gui_codec_state_unlock(state);

  return G_SOURCE_REMOVE;
}

SUPRIVATE gboolean
suscan_gui_codec_async_display_error(gpointer user_data)
{
  struct suscan_gui_codec_state *state =
      (struct suscan_gui_codec_state *) user_data;

  suscan_gui_codec_state_lock(state);

  if (state->state != SUSCAN_GUI_CODEC_STATE_ORPHAN) {
    if (state->progress.updated && state->progress.message != NULL)
      suscan_error(
          state->owner->inspector->gui,
          "Codec error",
          "Codec error: %s",
          state->progress.message);
    else
      suscan_error(
          state->owner->inspector->gui,
          "Codec error",
          "Internal codec error");
  }

  suscan_gui_codec_state_unlock(state);

  return G_SOURCE_REMOVE;
}

SUPRIVATE gboolean
suscan_gui_codec_async_unref(gpointer user_data)
{
  struct suscan_gui_codec_state *state =
      (struct suscan_gui_codec_state *) user_data;

  suscan_gui_codec_state_lock(state);

  if (state->state != SUSCAN_GUI_CODEC_STATE_ORPHAN)
    gtk_widget_hide(GTK_WIDGET(state->owner->inspector->progressDialog));

  if (!suscan_gui_codec_state_unref_internal(state))
    suscan_gui_codec_state_unlock(state);

  return G_SOURCE_REMOVE;
}

SUPRIVATE void
suscan_gui_codec_notify_progress(struct suscan_gui_codec_state *state)
{
  g_idle_add(suscan_gui_codec_async_parse_progress, state);
}

SUPRIVATE void
suscan_gui_codec_notify_data(struct suscan_gui_codec_state *state)
{
  g_idle_add(suscan_gui_codec_async_append_data, state);
}


SUPRIVATE void
suscan_gui_codec_notify_error(struct suscan_gui_codec_state *state)
{
  g_idle_add(suscan_gui_codec_async_display_error, state);
}

SUPRIVATE void
suscan_gui_codec_notify_unref(struct suscan_gui_codec_state *state)
{
  g_idle_add(suscan_gui_codec_async_unref, state);
}

SUPRIVATE SUBOOL
suscan_gui_codec_work(
    struct suscan_mq *mq_out,
    void *wk_private,
    void *cb_private)
{
  struct suscan_gui_codec_state *state =
      (struct suscan_gui_codec_state *) cb_private;
  SUSDIFF got;
  SUSDIFF size;
  SUBOOL busy = SU_TRUE;

  /* Check whether it was canceled by used (or orphaned, etc) */
  suscan_gui_codec_state_lock(state);
  busy = state->state == SUSCAN_GUI_CODEC_STATE_BUSY;
  suscan_gui_codec_state_unlock(state);

  /*
   * From here, the codec state may transfer to CANCELING or ORPHAN.
   * We verify that case before sending any idle callbacks to the user
   */
  if (!busy)
    goto done;

  /*
   * Time to do some processing, with SUSCAN_GUI_CODEC_MAX_BLOCK_SIZE
   * at most to avoid hogging the worker
   */
  size = state->input_len - state->ptr;
  if (size > SUSCAN_GUI_CODEC_MAX_BLOCK_SIZE)
    size = SUSCAN_GUI_CODEC_MAX_BLOCK_SIZE;

  /* Default progress */
  state->_progress.progress =
      (SUFLOAT) (state->ptr + 1) / (SUFLOAT) state->input_len;

  /* We are dealing with the private part here. No worries about concurrency */
  got = suscan_codec_feed(
      state->codec,
      &state->_output,
      &state->_progress,
      state->input + state->ptr,
      size);

  /* Some basic preconditions */
  if (got < SUSCAN_PROCESS_CODE_MIN) {
    SU_ERROR("Invalid codec return value %d\n", got);
    busy = SU_FALSE;
    goto done;
  }

  if (got > size) {
    SU_ERROR("Codec processed more bytes than provided (%d > %d)\n", got, size);
    busy = SU_FALSE;
    goto done;
  }

  switch (got) {
    case SUSCAN_PROCESS_CODE_ERROR:
      /* TODO: Notify user about error in data */
      busy = SU_FALSE;
      break;

    case SUSCAN_PROCESS_CODE_EOS:
      /* TODO: Notify user about unexpected end of stream */
      busy = SU_FALSE;
      break;

    default:
      state->ptr += got;
  }

  suscan_gui_codec_state_lock(state);
  /* vvvvvvvvvvvvvvvvvvvvvvv UPDATE SHARED PART vvvvvvvvvvvvvvvvvvvvvvvvvvvvv */
  busy = state->state == SUSCAN_GUI_CODEC_STATE_BUSY;
  if (busy) {
    /* Not necessary to send anything to the GUI if this was canceled */
    if (state->_progress.updated) {
      /* Discard previous message */
      if (state->progress.message != NULL)
        free(state->progress.message);

      /* Transfer new progress to GUI and discard old */
      state->progress = state->_progress;
      state->_progress.message = NULL;
      state->_progress.updated = SU_FALSE;

      /* Notify user */
      suscan_gui_codec_notify_progress(state);
    }

    /* Transfer new data to output grow buffer */
    if (grow_buf_get_size(&state->_output) > 0) {
      if (grow_buf_transfer(&state->output, &state->_output) == -1) {
        SU_ERROR("Transfer grow buffer data failed\n");
        suscan_gui_codec_notify_error(state);
      } else {
        suscan_gui_codec_notify_data(state);
      }
    }

    /* Size equals to len, processing has finished. Notify GUI? */
    if (state->ptr == state->input_len) {
      state->state = SUSCAN_GUI_CODEC_STATE_DONE;
      busy = SU_FALSE;
    }
  }

  /* ^^^^^^^^^^^^^^^^^^^ END OF SHARED PART UPDATE ^^^^^^^^^^^^^^^^^^^^^^^^^^^*/
  suscan_gui_codec_state_unlock(state);

done:
  /* This is always the last idle callback sent to the GUI */
  if (!busy)
    suscan_gui_codec_notify_unref(state);

  return busy;
}

struct suscan_gui_codec_state *
suscan_gui_codec_state_new(
    suscan_codec_t *codec,
    struct suscan_gui_codec *owner,
    const SuGtkSymView *source)
{
  struct suscan_gui_codec_state *new = NULL;
  guint start;
  guint end;
  const uint8_t *bytes;

  unsigned int i, j;

  SU_TRYCATCH(
      new = calloc(1, sizeof (struct suscan_gui_codec_state)),
      goto fail);

  pthread_mutex_init(&new->mutex, NULL);

  new->state = SUSCAN_GUI_CODEC_STATE_BUSY;
  new->ref_count = 1;
  new->owner = owner;
  new->codec = codec;

  bytes = sugtk_sym_get_buffer_bytes(source);
  if (!sugtk_sym_view_get_selection(source, &start, &end)) {
    start = 0;
    end = sugtk_sym_get_buffer_size(source) / SUGTK_SYM_VIEW_STRIDE_ALIGN;
  }

  new->input_len = end - start;

  /* Copy all symbols to input */
  SU_TRYCATCH(new->input = malloc(new->input_len * sizeof (SUBITS)), goto fail);

  j = start * SUGTK_SYM_VIEW_STRIDE_ALIGN;

  for (i = 0; i < new->input_len; ++i) {
    new->input[i] = sugtk_sym_view_pixel_to_code_helper(
        suscan_codec_get_input_bits_per_symbol(codec),
        bytes[j]);
    j += SUGTK_SYM_VIEW_STRIDE_ALIGN;
  }

  return new;

fail:
  if (new != NULL)
    suscan_gui_codec_state_destroy(new);

  return NULL;
}



/***************************** GUI handling ***********************************/
SUPRIVATE void
suscan_gui_codec_destroy_minimal(struct suscan_gui_codec *codec)
{
  unsigned int i;

  if (codec->input_buffer != NULL)
    free(codec->input_buffer);

  for (i = 0; i < codec->context_count; ++i)
    if (codec->context_list[i] != NULL)
      free(codec->context_list[i]);

  if (codec->context_list != NULL)
    free(codec->context_list);

  if (codec->builder != NULL)
    g_object_unref(G_OBJECT(codec->builder));

  free(codec);
}

void
suscan_gui_codec_destroy_hard(struct suscan_gui_codec *codec)
{
  /*
   * Destroy hard assumes that the worker does not exists any longer and
   * that we are in charge of disposing the codec state manually
   */

  if (codec->state != NULL)
    suscan_gui_codec_state_destroy(codec->state);

  suscan_gui_codec_destroy_minimal(codec);
}

void
suscan_gui_codec_destroy(struct suscan_gui_codec *codec)
{
  /* Normal destroy just marks the codec state as ORPHAN */
  if (codec->state != NULL) {
    suscan_gui_codec_state_lock(codec->state);

    /*
     * ... but we only do that if the worker is BUSY. Otherwise
     * it is already in its way (or ready) to be deleted
     */
    if (codec->state->state == SUSCAN_GUI_CODEC_STATE_BUSY) {
      codec->state->state = SUSCAN_GUI_CODEC_STATE_ORPHAN;
      codec->state->owner = NULL;
    }

    /* If unref didn't result in delition, unlock it */
    if (!suscan_gui_codec_state_unref_internal(codec->state))
      suscan_gui_codec_state_unlock(codec->state);
  }

  suscan_gui_codec_destroy_minimal(codec);
}

SUPRIVATE void
suscan_gui_codec_run_encoder(GtkWidget *widget, gpointer *data)
{
  struct suscan_gui_codec_context *ctx =
      (struct suscan_gui_codec_context *) data;

  /* This may happen if the context creation failed */
  if (ctx == NULL)
    return;

  if (!suscan_gui_codec_cfg_ui_assert_parent_gui(ctx->ui))
    return;  /* Weird */

  (void) suscan_gui_inspector_open_codec_tab(
      ctx->ui->inspector,
      ctx->ui,
      ctx->codec->output_bits,
      SUSCAN_CODEC_DIRECTION_FORWARDS,
      ctx->codec->symbolView);
}

SUPRIVATE void
suscan_gui_codec_run_codec(GtkWidget *widget, gpointer *data)
{
  struct suscan_gui_codec_context *ctx =
      (struct suscan_gui_codec_context *) data;

  /* This may happen if the context creation failed */
  if (ctx == NULL)
    return;

  if (!suscan_gui_codec_cfg_ui_assert_parent_gui(ctx->ui))
    return;  /* Weird */

  (void) suscan_gui_inspector_open_codec_tab(
      ctx->ui->inspector,
      ctx->ui,
      ctx->codec->output_bits,
      SUSCAN_CODEC_DIRECTION_BACKWARDS,
      ctx->codec->symbolView);
}


SUPRIVATE void *
suscan_gui_codec_create_context(
    void *private,
    struct suscan_gui_codec_cfg_ui *ui)
{
  struct suscan_gui_codec *codec = (struct suscan_gui_codec *) private;
  struct suscan_gui_codec_context *ctx = NULL;

  SU_TRYCATCH(
      ctx = malloc(sizeof (struct suscan_gui_codec_context)),
      goto fail);

  ctx->codec = codec;
  ctx->ui = ui;

  SU_TRYCATCH(PTR_LIST_APPEND_CHECK(codec->context, ctx) != -1, goto fail);

  return ctx;

fail:
  if (ctx != NULL)
    free(ctx);

  return NULL;
}

SUPRIVATE SUBOOL
suscan_gui_codec_load_all_widgets(struct suscan_gui_codec *codec)
{
  SU_TRYCATCH(
      codec->pageLabelEventBox =
          GTK_EVENT_BOX(gtk_builder_get_object(
              codec->builder,
              "ebPageLabel")),
          return SU_FALSE);

  SU_TRYCATCH(
      codec->pageLabel =
          GTK_LABEL(gtk_builder_get_object(
              codec->builder,
              "lPageLabel")),
          return SU_FALSE);

  SU_TRYCATCH(
      codec->codecGrid =
          GTK_GRID(gtk_builder_get_object(
              codec->builder,
              "grCodec")),
          return SU_FALSE);

  SU_TRYCATCH(
      codec->autoFitToggleButton =
          GTK_TOGGLE_TOOL_BUTTON(gtk_builder_get_object(
              codec->builder,
              "tbFitWidth")),
          return SU_FALSE);

  SU_TRYCATCH(
      codec->offsetSpinButton =
          GTK_SPIN_BUTTON(gtk_builder_get_object(
              codec->builder,
              "sbOffset")),
          return SU_FALSE);

  SU_TRYCATCH(
      codec->widthSpinButton =
          GTK_SPIN_BUTTON(gtk_builder_get_object(
              codec->builder,
              "sbWidth")),
          return SU_FALSE);

  /* Add symbol view */
  codec->symbolView = SUGTK_SYM_VIEW(sugtk_sym_view_new());

  SU_TRYCATCH(
      suscan_gui_inspector_populate_codec_menu(
          codec->inspector,
          codec->symbolView,
          suscan_gui_codec_create_context,
          codec,
          G_CALLBACK(suscan_gui_codec_run_encoder),
          G_CALLBACK(suscan_gui_codec_run_codec)),
      return SU_FALSE);

  gtk_grid_attach(
      codec->codecGrid,
      GTK_WIDGET(codec->symbolView),
      0, /* left */
      1, /* top */
      1, /* width */
      1 /* height */);

  gtk_widget_set_hexpand(GTK_WIDGET(codec->symbolView), TRUE);
  gtk_widget_set_vexpand(GTK_WIDGET(codec->symbolView), TRUE);

  gtk_widget_show(GTK_WIDGET(codec->symbolView));

  return SU_TRUE;
}

struct suscan_gui_codec *
suscan_gui_codec_new(
    struct suscan_gui_inspector *inspector,
    const struct suscan_codec_class *class,
    uint8_t bits_per_symbol,
    suscan_config_t *config,
    unsigned int direction,
    const SuGtkSymView *source)
{
  struct suscan_gui_codec *new = NULL;
  suscan_codec_t *codec = NULL;
  char *page_label = NULL;

  SU_TRYCATCH(new = calloc(1, sizeof (struct suscan_gui_codec)), goto fail);

  /* This is the underlying codec object used by suscan_gui_codec */
  SU_TRYCATCH(
      codec = suscan_codec_class_make_codec(
          class,
          bits_per_symbol,
          config,
          direction),
      goto fail);

  new->input_bits = bits_per_symbol;
  new->output_bits = suscan_codec_get_output_bits_per_symbol(codec);
  new->desc = class->desc;
  new->direction = direction;
  new->index = -1;
  new->class = class;
  new->inspector = inspector;

  SU_TRYCATCH(
      new->builder = gtk_builder_new_from_file(
          PKGDATADIR "/gui/codec-tab.glade"),
      goto fail);

  SU_TRYCATCH(suscan_gui_codec_load_all_widgets(new), goto fail);

  gtk_builder_connect_signals(new->builder, new);

  SU_TRYCATCH(
      page_label = strbuild(
          "%s with %s",
          direction == SUSCAN_CODEC_DIRECTION_BACKWARDS ? "Decode" : "Encode",
          class->desc),
      goto fail);


  gtk_label_set_text(new->pageLabel, page_label);

  free(page_label);
  page_label = NULL;

  /* Create codec state. This is what actually does the job */
  SU_TRYCATCH(
      new->state = suscan_gui_codec_state_new(codec, new, source),
      goto fail);

  codec = NULL;

  /* Owned also by the worker */
  suscan_gui_codec_state_addref_internal(new->state);

  /* Must be the last thing to be added */
  SU_TRYCATCH(
      suscan_gui_inspector_push_task(
          inspector,
          suscan_gui_codec_work,
          new->state),
      goto fail);

  return new;

fail:
  /*
   * As the last operation that may fail is suscan_gui_inspector_push_task,
   * if we even managed to create the new->state, it will not be in the
   * inspector's worker queue, so we should use a hard destroy here.
   */
  if (new != NULL)
    suscan_gui_codec_destroy_hard(new);

  if (codec != NULL)
    suscan_codec_destroy(codec);

  if (page_label != NULL)
    free(page_label);

  return NULL;
}

void
suscan_on_close_codec_tab(GtkWidget *widget, gpointer data)
{
  struct suscan_gui_codec *codec = (struct suscan_gui_codec *) data;

  suscan_gui_inspector_remove_codec(codec->inspector, codec);

  /*
   * Use soft destroy: the worker is running, and a decoder task
   * may be running in the meantime.
   */
  suscan_gui_codec_destroy(codec);
}

/******************** Decoder view toolbar buttons ****************************/
void
suscan_codec_on_save(
    GtkWidget *widget,
    gpointer data)
{
  struct suscan_gui_codec *codec = (struct suscan_gui_codec *) data;
  char *new_fname = NULL;

  SU_TRYCATCH(
      new_fname = strbuild(
          "%s-output-%s-%dbpp.log",
          codec->direction == SUSCAN_CODEC_DIRECTION_BACKWARDS ?
              "codec" :
              "encoder",
          codec->desc,
          codec->output_bits),
      goto done);

  SU_TRYCATCH(
      sugtk_sym_view_save_helper(
          codec->symbolView,
          "Save symbol view",
          new_fname,
          codec->output_bits),
      goto done);

done:
  if (new_fname != NULL)
    free(new_fname);
}

SUPRIVATE void
suscan_gui_codec_update_spin_buttons(struct suscan_gui_codec *codec)
{
  if (gtk_toggle_tool_button_get_active(
      GTK_TOGGLE_TOOL_BUTTON(codec->autoFitToggleButton)))
    gtk_spin_button_set_value(
        codec->widthSpinButton,
        sugtk_sym_view_get_width(codec->symbolView));
}

void
suscan_codec_on_zoom_in(
    GtkWidget *widget,
    gpointer data)
{
  struct suscan_gui_codec *codec = (struct suscan_gui_codec *) data;
  guint curr_width = sugtk_sym_view_get_width(codec->symbolView);
  guint curr_zoom = sugtk_sym_view_get_zoom(codec->symbolView);

  curr_zoom <<= 1;

  if (curr_width < curr_zoom)
    curr_zoom = curr_width;

  sugtk_sym_view_set_zoom(codec->symbolView, curr_zoom);

  suscan_gui_codec_update_spin_buttons(codec);
}


void
suscan_codec_on_zoom_out(
    GtkWidget *widget,
    gpointer data)
{
  struct suscan_gui_codec *codec = (struct suscan_gui_codec *) data;
  guint curr_width = sugtk_sym_view_get_width(codec->symbolView);
  guint curr_zoom = sugtk_sym_view_get_zoom(codec->symbolView);

  curr_zoom >>= 1;

  if (curr_zoom < 1)
    curr_zoom = 1;

  sugtk_sym_view_set_zoom(codec->symbolView, curr_zoom);

  suscan_gui_codec_update_spin_buttons(codec);
}

void
suscan_codec_on_toggle_autofit(
    GtkWidget *widget,
    gpointer data)
{
  struct suscan_gui_codec *codec = (struct suscan_gui_codec *) data;
  gboolean active;

  active = gtk_toggle_tool_button_get_active(GTK_TOGGLE_TOOL_BUTTON(widget));

  sugtk_sym_view_set_autofit(codec->symbolView, active);
  gtk_widget_set_sensitive(GTK_WIDGET(codec->widthSpinButton), !active);
}

void
suscan_codec_on_set_offset(
    GtkWidget *widget,
    gpointer data)
{
  struct suscan_gui_codec *codec = (struct suscan_gui_codec *) data;

  sugtk_sym_view_set_offset(
      codec->symbolView,
      gtk_spin_button_get_value(codec->offsetSpinButton));
}

void
suscan_codec_on_set_width(
    GtkWidget *widget,
    gpointer data)
{
  struct suscan_gui_codec *codec = (struct suscan_gui_codec *) data;

  if (!gtk_toggle_tool_button_get_active(
      GTK_TOGGLE_TOOL_BUTTON(codec->autoFitToggleButton)))
    sugtk_sym_view_set_width(
        codec->symbolView,
        gtk_spin_button_get_value(codec->widthSpinButton));
}
