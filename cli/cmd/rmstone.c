/*

  Copyright (C) 2020 Gonzalo José Carracedo Carballal

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

#define SU_LOG_DOMAIN "cli-rms"

#include <sigutils/log.h>
#include <sigutils/sampling.h>
#include <sigutils/ncqo.h>
#include <analyzer/analyzer.h>
#include <string.h>

#include <cli/cli.h>
#include <cli/cmds.h>
#include <cli/chanloop.h>
#include <cli/audio.h>

#define SAMPLES         150000
#define UPDATES_PER_MSG 10
#define AUDIO_TONE_MIN_HZ 880.
#define AUDIO_TONE_MAX_HZ (2 * AUDIO_TONE_MIN_HZ)
#define AUDIO_TONE_MIN_DB -60.
#define AUDIO_TONE_MAX_DB -50.

#define AUDIO_TONE_DB_RANGE (AUDIO_TONE_MAX_DB - AUDIO_TONE_MIN_DB)
#define AUDIO_TONE_HZ_RANGE (AUDIO_TONE_MAX_HZ - AUDIO_TONE_MIN_HZ)

PTR_LIST(SUPRIVATE suscan_source_config_t, rms_config);

struct rmstone_state {
  int samples;
  SUFLOAT c;
  SUFLOAT sum;
  SUBOOL  failed;
  SUFLOAT prev_db;
  SUFLOAT curr_db;
  SUBOOL  rms_changed;
  unsigned int updates;
  unsigned int samp_rate;
  su_ncqo_t afo;
  suscli_audio_player_t *player;
};

SUPRIVATE void
rmstone_state_finalize(struct rmstone_state *self)
{
  if (self->player != NULL)
    suscli_audio_player_destroy(self->player);

  memset(self, 0, sizeof (struct rmstone_state));
}

SUPRIVATE void
rmstone_state_mark_failed(struct rmstone_state *self)
{
  self->failed = SU_TRUE;
}

SUPRIVATE SUBOOL
suscli_rmstone_audio_start_cb(suscli_audio_player_t *self, void *userdata)
{
  struct rmstone_state *state = (struct rmstone_state *) userdata;
  SUBOOL ok = SU_FALSE;

  state->samp_rate = suscli_audio_player_samp_rate(self);

  su_ncqo_set_freq(
      &state->afo,
      SU_ABS2NORM_FREQ(
          state->samp_rate,
          AUDIO_TONE_MIN_HZ));

  ok = SU_TRUE;

  return ok;
}

SUPRIVATE SUBOOL
suscli_rmstone_audio_play_cb(
    suscli_audio_player_t *self,
    SUFLOAT *buffer,
    size_t len,
    void *userdata)
{
  struct rmstone_state *state = (struct rmstone_state *) userdata;
  int i;
  SUFLOAT freq;
  SUBOOL ok = SU_FALSE;

  for (i = 0; i < len; ++i) {
    if (state->failed) {
      SU_ERROR("Aborting audio playback due errors\n");
      goto fail;
    }

    /* TODO: lock */
    if (state->rms_changed) {
      state->rms_changed = SU_FALSE;
      freq  = (state->curr_db - AUDIO_TONE_MIN_DB) / AUDIO_TONE_DB_RANGE
              * AUDIO_TONE_HZ_RANGE + AUDIO_TONE_MIN_HZ;
      state->prev_db     = state->curr_db;
      su_ncqo_set_freq(&state->afo, SU_ABS2NORM_FREQ(state->samp_rate, freq));
    }

    buffer[i] = (1. / 16.) * SU_C_REAL(su_ncqo_read_i(&state->afo));
  }

  ok = SU_TRUE;

fail:
  return ok;
}

SUPRIVATE void
suscli_rmstone_audio_stop_cb(suscli_audio_player_t *self, void *userdata)
{
  /* No-op */
}

SUPRIVATE void
suscli_rmstone_audio_error_cb(suscli_audio_player_t *self, void *userdata)
{
  struct rmstone_state *state = (struct rmstone_state *) userdata;

  rmstone_state_mark_failed(state);
}


SUPRIVATE SUBOOL
rmstone_state_init(struct rmstone_state *state)
{
  SUBOOL ok = SU_FALSE;
  struct suscli_audio_player_params params =
      suscli_audio_player_params_INITIALIZER;

  memset(state, 0, sizeof(struct rmstone_state));

  params.userdata = state;
  params.start    = suscli_rmstone_audio_start_cb;
  params.play     = suscli_rmstone_audio_play_cb;
  params.stop     = suscli_rmstone_audio_stop_cb;
  params.error    = suscli_rmstone_audio_error_cb;

  SU_TRYCATCH(state->player = suscli_audio_player_new(&params), goto fail);

  ok = SU_TRUE;

fail:
  if (!ok)
    rmstone_state_finalize(state);

  return ok;
}


SUPRIVATE SUBOOL
walk_all_sources(suscan_source_config_t *config, void *privdata)
{
  return PTR_LIST_APPEND_CHECK(rms_config, config) != -1;
}

SUPRIVATE SUBOOL
suscli_rmstone_on_data_cb(
    suscan_analyzer_t *self,
    const SUCOMPLEX *data,
    size_t size,
    void *userdata)
{
  int i;
  SUFLOAT y, tmp;
  struct rmstone_state *state = (struct rmstone_state *) userdata;

  for (i = 0; i < size && !state->failed; ++i) {
    y = SU_C_REAL(data[i] * SU_C_CONJ(data[i]));
    tmp = state->sum + y;
    state->c = (tmp - state->sum) - y;
    state->sum = tmp;

    if (++state->samples >= SAMPLES) {
      if (++state->updates > UPDATES_PER_MSG) {
        state->updates = 0;
        printf("RMS = %.3f dB\r", SU_POWER_DB(state->sum / state->samples));
        fflush(stdout);
      }

      state->curr_db = SU_POWER_DB(state->sum / state->samples);

      state->c = state->sum = state->samples = 0;
      state->rms_changed = SU_TRUE;
    }
  }

  if (state->failed) {
    SU_ERROR("Stopping capture due to errors\n");
    return SU_FALSE;
  }

  return SU_TRUE;
}

SUBOOL
suscli_rmstone_cb(const hashlist_t *params)
{
  suscli_chanloop_t *chanloop = NULL;
  struct suscli_chanloop_params chanloop_params =
      suscli_chanloop_params_INITIALIZER;
  struct rmstone_state state;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(rmstone_state_init(&state), goto fail);

  chanloop_params.on_data  = suscli_rmstone_on_data_cb;
  chanloop_params.userdata = &state;

  SU_TRYCATCH(suscan_source_config_walk(walk_all_sources, 0), goto fail);

  SU_TRYCATCH(
      chanloop = suscli_chanloop_open(
          &chanloop_params,
          rms_config_list[3]),
      goto fail);

  SU_TRYCATCH(suscli_chanloop_work(chanloop), goto fail);

  ok = SU_TRUE;

fail:
  if (!ok)
    rmstone_state_mark_failed(&state);

  if (chanloop != NULL)
    suscli_chanloop_destroy(chanloop);

  rmstone_state_finalize(&state);

  return ok;
}
