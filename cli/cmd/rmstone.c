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

#define SUSCLI_DEFAULT_RMS_INTERVAL_MS  50
#define SUSCLI_DEFAULT_DISP_INTERVAL_MS 500
#define SUSCLI_DEFAULT_VOLUME           12.5
#define SUSCLI_DEFAULT_SCALE            0.5 /* In dBs */

#define AUDIO_TONE_MIN_HZ 220
#define AUDIO_TONE_MAX_HZ (16 * AUDIO_TONE_MIN_HZ)
#define AUDIO_TONE_MIN_DB -70.
#define AUDIO_TONE_MAX_DB -10.

#define AUDIO_TONE_DB_RANGE (AUDIO_TONE_MAX_DB - AUDIO_TONE_MIN_DB)
#define AUDIO_TONE_HZ_RANGE (AUDIO_TONE_MAX_HZ - AUDIO_TONE_MIN_HZ)

PTR_LIST(SUPRIVATE suscan_source_config_t, rms_config);

enum suscli_rmstone_mode {
  SUSCLI_RMSTONE_MODE_TONE,
  SUSCLI_RMSTONE_MODE_TWO_TONES,
  SUSCLI_RMSTONE_MODE_BEEPER
};

struct suscli_rmstone_params {
  suscan_source_config_t *profile;
  const char *mode;
  SUFLOAT db_min;
  SUFLOAT db_max;
  SUFLOAT freq_min;
  SUFLOAT freq_max;
  SUFLOAT rms_interval;
  SUFLOAT disp_interval;
  SUFLOAT scale;
  SUFLOAT volume;

  /* Precalculated terms */
  SUFLOAT k;
};

struct suscli_rmstone_state {
  struct suscli_rmstone_params params;
  unsigned int update_ctr;
  unsigned int disp_ctr;

  SUFLOAT c;
  SUFLOAT sum;
  SUBOOL  failed;

  SUFLOAT prev_db;
  SUFLOAT curr_db;

  SUFLOAT prev_pwr;
  SUFLOAT curr_pwr;

  SUBOOL  rms_changed;

  SUBOOL  capturing;

  unsigned int samp_per_update;
  unsigned int samp_per_disp;

  unsigned int samp_rate;
  su_ncqo_t afo;
  suscli_audio_player_t *player;
};

SUPRIVATE suscan_source_config_t *
suscli_rmstone_params_lookup_profile(const char *name)
{
  int i;

  for (i = 0; i < rms_config_count; ++i)
    if (suscan_source_config_get_label(rms_config_list[i]) != NULL
        && strcasecmp(
            suscan_source_config_get_label(rms_config_list[i]),
            name) == 0)
      return rms_config_list[i];

  return NULL;
}

SUPRIVATE void
suscli_rmstone_params_debug(const struct suscli_rmstone_params *self)
{
  fprintf(stderr, "Tone generator parameter summary:\n");
  fprintf(stderr, "  Profile: %s\n", suscan_source_config_get_label(self->profile));
  fprintf(stderr, "  Mode: %s\n", self->mode);
  fprintf(stderr, "  Dynamic range: %g dB - %g dB (%g dB)\n", self->db_min, self->db_max, self->db_max - self->db_min);
  fprintf(stderr, "  Audio frequency range: %g Hz - %g Hz", self->freq_min, self->freq_max);
  fprintf(stderr, "  RMS update interval: %g ms\n", self->rms_interval);
  fprintf(stderr, "  Display interval: %g ms\n", self->disp_interval);
  fprintf(stderr, "  Tone scale: %g dB\n", self->scale);
  fprintf(stderr, "  Volume: %g%%\n", self->volume);
  fprintf(stderr, "  K: %g\n", self->k);
}

SUPRIVATE SUBOOL
suscli_rmstone_params_parse(
    struct suscli_rmstone_params *self,
    const hashlist_t *p)
{
  suscan_source_config_t *profile;
  const char *profile_name;
  int profile_id = rms_config_count;
  SUBOOL ok = SU_FALSE;

  if (suscli_param_read_int(p, "profile", &profile_id, profile_id)) {
    if (profile_id > 0 || profile_id <= rms_config_count) {
      profile = rms_config_list[profile_id - 1];
    } else {
      SU_ERROR("Profile index `%d' out ouf bounds.\n", profile_id);
      goto fail;
    }
  } else {
    SU_TRYCATCH(
        suscli_param_read_string(
            p,
            "profile",
            &profile_name,
            NULL),
        goto fail);
    if (profile_name == NULL) {
      profile = rms_config_list[profile_id - 1];
    } else {
      if ((profile = suscli_rmstone_params_lookup_profile(profile_name)) == NULL) {
        SU_ERROR("Profile `%d' does not exist.\n", profile_name);
        goto fail;
      }
    }
  }

  self->profile = profile;

  SU_TRYCATCH(
      suscli_param_read_string(p, "mode", &self->mode, "tone"),
      goto fail);
  SU_TRYCATCH(
      suscli_param_read_float(p, "db_min", &self->db_min, AUDIO_TONE_MIN_DB),
      goto fail);
  SU_TRYCATCH(
      suscli_param_read_float(p, "db_max", &self->db_max, AUDIO_TONE_MAX_DB),
      goto fail);
  SU_TRYCATCH(
      suscli_param_read_float(p, "freq_min", &self->freq_min, AUDIO_TONE_MIN_HZ),
      goto fail);
  SU_TRYCATCH(
      suscli_param_read_float(p, "freq_max", &self->freq_max, AUDIO_TONE_MAX_HZ),
      goto fail);
  SU_TRYCATCH(
      suscli_param_read_float(p, "rms_interval", &self->rms_interval, SUSCLI_DEFAULT_RMS_INTERVAL_MS),
      goto fail);
  SU_TRYCATCH(
      suscli_param_read_float(p, "disp_interval", &self->disp_interval, SUSCLI_DEFAULT_DISP_INTERVAL_MS),
      goto fail);
  SU_TRYCATCH(
      suscli_param_read_float(p, "scale", &self->scale, SUSCLI_DEFAULT_SCALE),
      goto fail);
  SU_TRYCATCH(
      suscli_param_read_float(p, "volume", &self->volume, SUSCLI_DEFAULT_VOLUME),
      goto fail);

  self->k = SU_LOG(self->freq_max / self->freq_min);

  suscli_rmstone_params_debug(self);

  ok = SU_TRUE;

fail:
  return ok;
}

SUPRIVATE void
suscli_rmstone_state_finalize(struct suscli_rmstone_state *self)
{
  if (self->player != NULL)
    suscli_audio_player_destroy(self->player);

  memset(self, 0, sizeof (struct suscli_rmstone_state));
}

SUPRIVATE void
suscli_rmstone_state_mark_failed(struct suscli_rmstone_state *self)
{
  self->failed = SU_TRUE;
}

SUPRIVATE SUBOOL
suscli_rmstone_audio_start_cb(suscli_audio_player_t *self, void *userdata)
{
  struct suscli_rmstone_state *state = (struct suscli_rmstone_state *) userdata;
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
  struct suscli_rmstone_state *state = (struct suscli_rmstone_state *) userdata;
  int i;
  SUFLOAT freq;
  SUFLOAT normalized;
  SUFLOAT db;
  SUBOOL ok = SU_FALSE;

  if (state->capturing) {
    for (i = 0; i < len; ++i) {
      if (state->failed) {
        SU_ERROR("Aborting audio playback due errors\n");
        goto fail;
      }

      /* TODO: lock */
      if (state->rms_changed) {
        state->rms_changed = SU_FALSE;
        db = state->curr_db;

        if (state->params.scale > 0)
            db = state->params.scale * SU_FLOOR(db / state->params.scale);

        normalized = (db - state->params.db_min) /
            (state->params.db_max - state->params.db_min);
        freq = AUDIO_TONE_MIN_HZ * SU_C_EXP(state->params.k * normalized);


        state->prev_db     = state->curr_db;
        su_ncqo_set_freq(&state->afo, SU_ABS2NORM_FREQ(state->samp_rate, freq));
      }

      buffer[i] =
          1e-2 * state->params.volume * SU_C_REAL(su_ncqo_read_i(&state->afo));
    }
  } else {
    memset(buffer, 0, sizeof(SUFLOAT) * len);
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
  struct suscli_rmstone_state *state = (struct suscli_rmstone_state *) userdata;

  suscli_rmstone_state_mark_failed(state);
}


SUPRIVATE SUBOOL
suscli_rmstone_state_init(
    struct suscli_rmstone_state *state, const hashlist_t *params)
{
  SUBOOL ok = SU_FALSE;
  struct suscli_audio_player_params audio_params =
      suscli_audio_player_params_INITIALIZER;

  memset(state, 0, sizeof(struct suscli_rmstone_state));

  SU_TRYCATCH(suscli_rmstone_params_parse(&state->params, params), goto fail);

  audio_params.userdata = state;
  audio_params.start    = suscli_rmstone_audio_start_cb;
  audio_params.play     = suscli_rmstone_audio_play_cb;
  audio_params.stop     = suscli_rmstone_audio_stop_cb;
  audio_params.error    = suscli_rmstone_audio_error_cb;

  SU_TRYCATCH(
      state->player = suscli_audio_player_new(&audio_params),
      goto fail);

  ok = SU_TRUE;

fail:
  if (!ok)
    suscli_rmstone_state_finalize(state);

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
  struct suscli_rmstone_state *state = (struct suscli_rmstone_state *) userdata;

  for (i = 0; i < size && !state->failed; ++i) {
    y = SU_C_REAL(data[i] * SU_C_CONJ(data[i]));
    tmp = state->sum + y;
    state->c = (tmp - state->sum) - y;
    state->sum = tmp;

    if (++state->update_ctr >= state->samp_per_update) {
      state->curr_db = SU_POWER_DB(state->sum / state->update_ctr);
      state->c = state->sum = state->update_ctr = 0;
      state->rms_changed = SU_TRUE;
    }

    if (++state->disp_ctr >= state->samp_per_disp) {
      state->disp_ctr = 0;
      printf("RMS = %.3f dB\r", state->curr_db);
      fflush(stdout);
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
  struct suscli_rmstone_state state;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(suscan_source_config_walk(walk_all_sources, 0), goto fail);
  SU_TRYCATCH(suscli_rmstone_state_init(&state, params), goto fail);

  chanloop_params.on_data  = suscli_rmstone_on_data_cb;
  chanloop_params.userdata = &state;

  SU_TRYCATCH(
      chanloop = suscli_chanloop_open(
          &chanloop_params,
          state.params.profile),
      goto fail);

  state.samp_per_update
    = 1e-3 * state.params.rms_interval * suscli_chanloop_get_equiv_fs(chanloop);
  state.samp_per_disp
      = 1e-3 * state.params.disp_interval * suscli_chanloop_get_equiv_fs(chanloop);
  state.capturing = SU_TRUE;

  printf("Timebase: %d, %d\n", state.samp_per_update, state.samp_per_disp);

  SU_TRYCATCH(suscli_chanloop_work(chanloop), goto fail);

  ok = SU_TRUE;

fail:
  if (!ok)
    suscli_rmstone_state_mark_failed(&state);

  if (chanloop != NULL)
    suscli_chanloop_destroy(chanloop);

  suscli_rmstone_state_finalize(&state);

  return ok;
}
