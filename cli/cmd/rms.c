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

#define AUDIO_TONE_BEEP_LONG_MS 1000
#define AUDIO_TONE_BEEP_SHORT_MS  10

#define AUDIO_TONE_DB_RANGE (AUDIO_TONE_MAX_DB - AUDIO_TONE_MIN_DB)
#define AUDIO_TONE_HZ_RANGE (AUDIO_TONE_MAX_HZ - AUDIO_TONE_MIN_HZ)

enum suscli_rms_mode {
  SUSCLI_RMSTONE_MODE_TONE,
  SUSCLI_RMSTONE_MODE_TWO_TONES,
  SUSCLI_RMSTONE_MODE_BEEPER
};

struct suscli_rms_params {
  suscan_source_config_t *profile;
  enum suscli_rms_mode mode;
  SUBOOL  audio;
  SUFLOAT db_min;
  SUFLOAT db_max;
  SUFLOAT freq_min;
  SUFLOAT freq_max;
  SUFLOAT rms_interval;
  SUFLOAT disp_interval;
  SUFLOAT scale;
  SUFLOAT volume;
  SUFLOAT beep_long;
  SUFLOAT beep_short;

  /* Precalculated terms */
  enum suscli_rms_mode mode_enum;
  SUFLOAT k;
};

struct suscli_rms_state {
  struct suscli_rms_params params;

  unsigned int samp_per_update;
  unsigned int samp_per_disp;

  int samp_per_short_beep;
  int samp_per_long_beep;

  unsigned int beep_ctr;
  unsigned int update_ctr;
  unsigned int disp_ctr;

  SUFLOAT c;
  SUFLOAT sum;
  SUBOOL  failed;

  SUFLOAT prev_db;
  SUFLOAT curr_db;

  SUFLOAT freq1;
  SUFLOAT freq2;
  SUBOOL  second_cycle;

  /* Used for beeper */
  int samp_per_beep_cycle;

  SUBOOL  rms_changed;

  SUBOOL  capturing;

  unsigned int samp_rate;
  su_ncqo_t afo;
  suscli_audio_player_t *player;
};

SUPRIVATE void suscli_rms_state_mark_failed(
    struct suscli_rms_state *self);

/***************************** Audio callbacks ********************************/
SUPRIVATE SUBOOL
suscli_rms_audio_start_cb(suscli_audio_player_t *self, void *userdata)
{
  struct suscli_rms_state *state = (struct suscli_rms_state *) userdata;
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
suscli_rms_audio_play_cb(
    suscli_audio_player_t *self,
    SUFLOAT *buffer,
    size_t len,
    void *userdata)
{
  struct suscli_rms_state *state = (struct suscli_rms_state *) userdata;
  int i;
  SUFLOAT normalized;
  SUBOOL  freq_changed = SU_FALSE;
  SUFLOAT db;
  SUBOOL ok = SU_FALSE;

  if (state->capturing) {
    for (i = 0; i < len; ++i) {
      if (state->failed) {
        SU_ERROR("Aborting audio playback due errors.\n");
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

        switch (state->params.mode) {
          case SUSCLI_RMSTONE_MODE_TONE:
            state->samp_per_beep_cycle = 0;
            state->samp_per_short_beep = 0;
            state->freq1 = 0;
            state->freq2 = state->params.freq_min
                * SU_C_EXP(state->params.k * normalized);
            freq_changed = SU_TRUE;
            break;

          case SUSCLI_RMSTONE_MODE_TWO_TONES:
            state->samp_per_beep_cycle = state->samp_per_long_beep;
            state->freq1 = state->params.freq_min;
            state->freq2 = state->params.freq_min
                * SU_C_EXP(state->params.k * normalized);
            break;

          case SUSCLI_RMSTONE_MODE_BEEPER:
            state->samp_per_beep_cycle
              = (1 - normalized) * state->samp_per_long_beep;

            if (state->samp_per_beep_cycle > state->samp_per_long_beep)
              state->samp_per_beep_cycle = state->samp_per_long_beep;
            else if (state->samp_per_beep_cycle < state->samp_per_short_beep)
              state->samp_per_beep_cycle = state->samp_per_short_beep;

            state->freq1 = state->params.freq_max;
            state->freq2 = 0;

            break;
        }

        state->prev_db = state->curr_db;
      }

      if (!state->second_cycle) {
      /* First (short) subcycle has ended. Switch to freq2 */
        if ((state->beep_ctr >= state->samp_per_short_beep) || freq_changed) {
            su_ncqo_set_freq(
                &state->afo,
                SU_ABS2NORM_FREQ(
                    state->samp_rate,
                    state->freq2));
            freq_changed = SU_FALSE;
            state->second_cycle = SU_TRUE;
        }
      }

      buffer[i] =
          1e-2 * state->params.volume * SU_C_REAL(su_ncqo_read_i(&state->afo));

      /* Full cycle has ended back to freq1 */
      if (++state->beep_ctr >= state->samp_per_beep_cycle) {
        su_ncqo_set_freq(
            &state->afo,
            SU_ABS2NORM_FREQ(
                state->samp_rate,
                state->freq1));
        state->beep_ctr = 0;
        state->second_cycle = SU_FALSE;
      }
    }
  } else {
    memset(buffer, 0, sizeof(SUFLOAT) * len);
  }

  ok = SU_TRUE;

fail:
  return ok;
}

SUPRIVATE void
suscli_rms_audio_stop_cb(suscli_audio_player_t *self, void *userdata)
{
  /* No-op */
}

SUPRIVATE void
suscli_rms_audio_error_cb(suscli_audio_player_t *self, void *userdata)
{
  struct suscli_rms_state *state = (struct suscli_rms_state *) userdata;

  suscli_rms_state_mark_failed(state);
}

/******************************* Parameter parsing ***************************/

SUPRIVATE SUBOOL
suscli_rms_param_read_mode(
    const hashlist_t *params,
    const char *key,
    enum suscli_rms_mode *out,
    enum suscli_rms_mode dfl)
{
  const char *value;
  SUBOOL ok = SU_FALSE;

  if ((value = hashlist_get(params, key)) != NULL) {
    if (strcasecmp(value, "default") == 0 || strcasecmp(value, "tone") == 0) {
      dfl = SUSCLI_RMSTONE_MODE_TONE;
    } else if (strcasecmp(value, "beeper") == 0) {
      dfl = SUSCLI_RMSTONE_MODE_BEEPER;
    } else if (strcasecmp(value, "2tones") == 0
        || strcasecmp(value, "two_tones") == 0
        || strcasecmp(value, "twotones") == 0) {
      dfl = SUSCLI_RMSTONE_MODE_TWO_TONES;
    } else {
      SU_ERROR("`%s' is not a valid mode.\n", value);
      goto fail;
    }
  }

  *out = dfl;

  ok = SU_TRUE;

fail:
  return ok;
}

SUPRIVATE const char *
suscli_rms_mode_to_string(enum suscli_rms_mode mode)
{
  switch (mode) {
    case SUSCLI_RMSTONE_MODE_TONE:
      return "TONE";
    case SUSCLI_RMSTONE_MODE_TWO_TONES:
      return "TWO_TONES";
    case SUSCLI_RMSTONE_MODE_BEEPER:
      return "BEEPER";
  }

  return "UNKNOWN";
}

SUPRIVATE void
suscli_rms_params_debug(const struct suscli_rms_params *self)
{
  fprintf(stderr, "Tone generator parameter summary:\n");
  fprintf(
      stderr,
      "  Profile: %s\n",
      suscan_source_config_get_label(self->profile));
  fprintf(
        stderr,
        "  RMS update interval: %g ms\n",
        self->rms_interval);
    fprintf(
        stderr,
        "  Display interval: %g ms\n",
        self->disp_interval);
  fprintf(
      stderr,
      "  Audio: %s\n",
      self->audio ? "ON" : "OFF");

  if (self->audio) {
    fprintf(
          stderr,
          "  Audio mode: %s\n",
          suscli_rms_mode_to_string(self->mode));
    fprintf(
          stderr,
          "  Audio frequency range: %g Hz - %g Hz",
          self->freq_min, self->freq_max);
    fprintf(
        stderr,
        "  Dynamic range: %g dB - %g dB (%g dB)\n",
        self->db_min, self->db_max, self->db_max - self->db_min);
    fprintf(
          stderr,
          "  Tone scale: %g dB\n",
          self->scale);
      fprintf(
          stderr,
          "  Beep timing: %g ms - %g ms\n",
          self->beep_short,
          self->beep_long);
      fprintf(
          stderr,
          "  Volume: %g%%\n",
          self->volume);
      fprintf(
          stderr,
          "  K: %g\n",
          self->k);
  }

}

SUPRIVATE SUBOOL
suscli_rms_params_parse(
    struct suscli_rms_params *self,
    const hashlist_t *p)
{
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
        suscli_param_read_profile(
            p,
            "profile",
            &self->profile),
        goto fail);

  if (self->profile == NULL) {
    SU_ERROR("Suscan is unable to load any valid profile\n");
    goto fail;
  }

  SU_TRYCATCH(
      suscli_param_read_bool(
          p,
          "audio",
          &self->audio,
          SU_FALSE),
      goto fail);

  SU_TRYCATCH(
      suscli_rms_param_read_mode(
          p,
          "mode",
          &self->mode,
          SUSCLI_RMSTONE_MODE_TONE),
      goto fail);

  SU_TRYCATCH(
      suscli_param_read_float(
          p,
          "db_min",
          &self->db_min,
          AUDIO_TONE_MIN_DB),
      goto fail);

  SU_TRYCATCH(
      suscli_param_read_float(
          p,
          "db_max",
          &self->db_max,
          AUDIO_TONE_MAX_DB),
      goto fail);

  SU_TRYCATCH(
      suscli_param_read_float(
          p,
          "freq_min",
          &self->freq_min,
          AUDIO_TONE_MIN_HZ),
      goto fail);

  SU_TRYCATCH(
      suscli_param_read_float(
          p,
          "freq_max",
          &self->freq_max,
          AUDIO_TONE_MAX_HZ),
      goto fail);

  SU_TRYCATCH(
      suscli_param_read_float(
          p,
          "rms_interval",
          &self->rms_interval,
          SUSCLI_DEFAULT_RMS_INTERVAL_MS),
      goto fail);

  SU_TRYCATCH(
      suscli_param_read_float(
          p,
          "disp_interval",
          &self->disp_interval,
          SUSCLI_DEFAULT_DISP_INTERVAL_MS),
      goto fail);

  SU_TRYCATCH(
      suscli_param_read_float(
          p,
          "beep_long",
          &self->beep_long,
          AUDIO_TONE_BEEP_LONG_MS),
      goto fail);

  SU_TRYCATCH(
      suscli_param_read_float(
          p,
          "beep_short",
          &self->beep_short,
          AUDIO_TONE_BEEP_SHORT_MS),
      goto fail);

  SU_TRYCATCH(
      suscli_param_read_float(
          p,
          "scale",
          &self->scale,
          SUSCLI_DEFAULT_SCALE),
      goto fail);

  SU_TRYCATCH(
      suscli_param_read_float(
          p,
          "volume",
          &self->volume,
          SUSCLI_DEFAULT_VOLUME),
      goto fail);

  self->k = SU_LOG(self->freq_max / self->freq_min);

  suscli_rms_params_debug(self);

  ok = SU_TRUE;

fail:
  return ok;
}

/****************************** State handling *******************************/
SUPRIVATE void
suscli_rms_state_finalize(struct suscli_rms_state *self)
{
  if (self->player != NULL)
    suscli_audio_player_destroy(self->player);

  memset(self, 0, sizeof (struct suscli_rms_state));
}

SUPRIVATE void
suscli_rms_state_mark_failed(struct suscli_rms_state *self)
{
  self->failed = SU_TRUE;
}

SUPRIVATE SUBOOL
suscli_rms_state_init(
    struct suscli_rms_state *state,
    const hashlist_t *params)
{
  SUBOOL ok = SU_FALSE;
  struct suscli_audio_player_params audio_params =
      suscli_audio_player_params_INITIALIZER;

  memset(state, 0, sizeof(struct suscli_rms_state));

  SU_TRYCATCH(suscli_rms_params_parse(&state->params, params), goto fail);

  /* User requested audio play */
  if (state->params.audio) {
    audio_params.userdata = state;
    audio_params.start    = suscli_rms_audio_start_cb;
    audio_params.play     = suscli_rms_audio_play_cb;
    audio_params.stop     = suscli_rms_audio_stop_cb;
    audio_params.error    = suscli_rms_audio_error_cb;

    SU_TRYCATCH(
        state->player = suscli_audio_player_new(&audio_params),
        goto fail);
  }

  ok = SU_TRUE;

fail:
  if (!ok)
    suscli_rms_state_finalize(state);

  return ok;
}
/****************************** Capture ***************************************/
SUPRIVATE SUBOOL
suscli_rms_on_data_cb(
    suscan_analyzer_t *self,
    const SUCOMPLEX *data,
    size_t size,
    void *userdata)
{
  int i;
  SUFLOAT y, tmp;
  struct suscli_rms_state *state = (struct suscli_rms_state *) userdata;

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
suscli_rms_cb(const hashlist_t *params)
{
  suscli_chanloop_t *chanloop = NULL;
  struct suscli_chanloop_params chanloop_params =
      suscli_chanloop_params_INITIALIZER;
  struct suscli_rms_state state;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(suscli_rms_state_init(&state, params), goto fail);

  chanloop_params.on_data  = suscli_rms_on_data_cb;
  chanloop_params.userdata = &state;

  SU_TRYCATCH(
      chanloop = suscli_chanloop_open(
          &chanloop_params,
          state.params.profile),
      goto fail);

  state.samp_per_update
    = 1e-3 * state.params.rms_interval
    * suscli_chanloop_get_equiv_fs(chanloop);
  state.samp_per_disp
      = 1e-3 * state.params.disp_interval
      * suscli_chanloop_get_equiv_fs(chanloop);

  state.samp_per_short_beep = 1e-3 * state.params.beep_short * state.samp_rate;
  state.samp_per_long_beep  = 1e-3 * state.params.beep_long * state.samp_rate;

  state.samp_per_beep_cycle = state.samp_per_long_beep;

  state.capturing = SU_TRUE;

  SU_TRYCATCH(suscli_chanloop_work(chanloop), goto fail);

  ok = SU_TRUE;

fail:
  if (!ok)
    suscli_rms_state_mark_failed(&state);

  if (chanloop != NULL)
    suscli_chanloop_destroy(chanloop);

  suscli_rms_state_finalize(&state);

  return ok;
}
