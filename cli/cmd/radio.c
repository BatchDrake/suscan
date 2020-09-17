/*

  Copyright (C) 2020 Gonzalo José Carracedo Carballal

  This program is free software: you can redistribute it and/or modify
  it under the teradio of the GNU Lesser General Public License as
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

#define SU_LOG_DOMAIN "cli-radio"

#include <sigutils/log.h>
#include <sigutils/sampling.h>
#include <sigutils/ncqo.h>
#include <analyzer/analyzer.h>
#include <analyzer/inspector/params.h>
#include <string.h>

#include <cli/cli.h>
#include <cli/cmds.h>
#include <cli/chanloop.h>
#include <cli/audio.h>

#include <poll.h>
#include <termios.h>
#include <signal.h>

struct suscli_radio_params {
  suscan_source_config_t *profile;
  enum suscan_inspector_audio_demod demod;
  SUFREQ  frequency;
  SUFLOAT volume_db;
  SUFLOAT cutoff;
  SUBOOL  squelch;
  SUFLOAT squelch_level;
  SUBOOL  disable_stderr;
  unsigned int samp_rate;
};

struct suscli_radio_state {
  struct suscli_radio_params params;
  suscli_chanloop_t *chanloop;
  unsigned int samp_rate;
  pthread_mutex_t mutex;
  SUBOOL mutex_initialized;
  SUCOMPLEX *audio_data_samples;
  SUSCOUNT audio_data_len;
  SUSCOUNT audio_data_alloc;
  SUBOOL halting;
  SUFREQ frequency;
  SUFREQ freq_step;
  struct termios old_termios;
  SUBOOL got_termios;

  suscli_audio_player_t *player;
};

SUPRIVATE void suscli_radio_state_mark_halting(
    struct suscli_radio_state *self);

SUPRIVATE struct suscli_radio_state *g_state;

SUPRIVATE const char *
suscli_radio_helper_format_frequency(SUFREQ freq, char *buf, size_t size)
{
  if (freq < 1e3)
    snprintf(buf, size, "%.0lf Hz", freq);
  else if (freq < 1e6)
    snprintf(buf, size, "%.3lf kHz", freq * 1e-3);
  else if (freq < 1e9)
    snprintf(buf, size, "%.6lf MHz", freq * 1e-6);
  else if (freq < 1e12)
    snprintf(buf, size, "%.9lf GHz", freq * 1e-9);
  else
    snprintf(buf, size, "%.12lf THz", freq * 1e-12);

  return buf;
}

/***************************** Audio callbacks ********************************/
SUPRIVATE SUBOOL
suscli_radio_audio_start_cb(suscli_audio_player_t *self, void *userdata)
{
  struct suscli_radio_state *state = (struct suscli_radio_state *) userdata;
  SUBOOL ok = SU_FALSE;

  state->samp_rate = suscli_audio_player_samp_rate(self);

  ok = SU_TRUE;

  return ok;
}

SUPRIVATE SUBOOL
suscli_radio_audio_play_cb(
    suscli_audio_player_t *self,
    SUFLOAT *buffer,
    size_t *len,
    void *userdata)
{
  struct suscli_radio_state *state = (struct suscli_radio_state *) userdata;
  SUBOOL ok = SU_FALSE;
  int i;

  if (!state->halting) {
    pthread_mutex_lock(&state->mutex);
    if (state->audio_data_len > *len)
      state->audio_data_len = *len;
    else
      *len = state->audio_data_len;

    for (i = 0; i < state->audio_data_len; ++i)
      buffer[i] = SU_C_REAL(state->audio_data_samples[i]);

    state->audio_data_len = 0;

    pthread_mutex_unlock(&state->mutex);

    ok = SU_TRUE;
  }

  return ok;
}

SUPRIVATE void
suscli_radio_audio_stop_cb(suscli_audio_player_t *self, void *userdata)
{
  /* No-op */
}

SUPRIVATE void
suscli_radio_audio_error_cb(suscli_audio_player_t *self, void *userdata)
{
  struct suscli_radio_state *state = (struct suscli_radio_state *) userdata;

  suscli_radio_state_mark_halting(state);
}

/******************************* Parameter parsing ***************************/

SUPRIVATE SUBOOL
suscli_radio_param_read_demod(
    const hashlist_t *params,
    const char *key,
    enum suscan_inspector_audio_demod *out,
    enum suscan_inspector_audio_demod dfl)
{
  const char *value;
  SUBOOL ok = SU_FALSE;

  if ((value = hashlist_get(params, key)) != NULL) {
    if (strcasecmp(value, "default") == 0 || strcasecmp(value, "fm") == 0) {
      dfl = SUSCAN_INSPECTOR_AUDIO_DEMOD_FM;
    } else if (strcasecmp(value, "am") == 0) {
      dfl = SUSCAN_INSPECTOR_AUDIO_DEMOD_AM;
    } else if (strcasecmp(value, "usb") == 0) {
      dfl = SUSCAN_INSPECTOR_AUDIO_DEMOD_USB;
    } else if (strcasecmp(value, "lsb") == 0) {
      dfl = SUSCAN_INSPECTOR_AUDIO_DEMOD_LSB;
    } else {
      SU_ERROR("`%s' is not a valid demodulator.\n", value);
      goto fail;
    }
  }

  *out = dfl;

  ok = SU_TRUE;

fail:
  return ok;
}

SUPRIVATE const char *
suscli_radio_demod_to_string(enum suscan_inspector_audio_demod demod)
{
  switch (demod) {
    case SUSCAN_INSPECTOR_AUDIO_DEMOD_DISABLED:
      return "DISABLED";

    case SUSCAN_INSPECTOR_AUDIO_DEMOD_AM:
      return "AM";

    case SUSCAN_INSPECTOR_AUDIO_DEMOD_FM:
      return "FM";

    case SUSCAN_INSPECTOR_AUDIO_DEMOD_USB:
      return "USB";

    case SUSCAN_INSPECTOR_AUDIO_DEMOD_LSB:
      return "LSB";
  }

  return "UNKNOWN";
}

SUPRIVATE void
suscli_radio_params_debug(const struct suscli_radio_params *self)
{
  char freqbuffer[24];
  const suscan_source_device_t *dev =
      suscan_source_config_get_device(self->profile);

  printf("Demodulator summary:\n");
  printf(
      "  Profile:       %s\n",
      suscan_source_config_get_label(self->profile));

  printf(
      "  Device:        %s\n",
      suscan_source_device_get_desc(dev));

  suscli_radio_helper_format_frequency(
      self->frequency,
      freqbuffer,
      sizeof(freqbuffer));

  printf(
      "  Frequency:     %s\n",
      freqbuffer);

  printf(
      "  Demodulator:   %s\n",
      suscli_radio_demod_to_string(self->demod));

  suscli_radio_helper_format_frequency(
      self->cutoff,
      freqbuffer,
      sizeof(freqbuffer));

  printf(
      "  Cutoff:        %s\n",
      freqbuffer);
  printf(
      "  Squelch:       %s\n",
      self->squelch ? "Yes" : "No");
  printf(
      "  Squelch level: %g\n",
      self->squelch_level);
  printf(
      "  Sample rate:   %d sp/s\n",
      self->samp_rate);
  printf(
      "  Volume:        %g dB\n",
      self->volume_db);
}

SUPRIVATE SUBOOL
suscli_radio_params_parse(
    struct suscli_radio_params *self,
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
      suscli_radio_param_read_demod(
          p,
          "demod",
          &self->demod,
          SUSCAN_INSPECTOR_AUDIO_DEMOD_FM),
      goto fail);

  SU_TRYCATCH(
      suscli_param_read_float(
          p,
          "volume",
          &self->volume_db,
          0),
      goto fail);

  SU_TRYCATCH(
      suscli_param_read_double(
          p,
          "frequency",
          &self->frequency,
          suscan_source_config_get_freq(self->profile)),
      goto fail);


  SU_TRYCATCH(
      suscli_param_read_int(
          p,
          "samp_rate",
          (int *) &self->samp_rate,
          44100),
      goto fail);

  SU_TRYCATCH(
      suscli_param_read_float(
          p,
          "cutoff",
          &self->cutoff,
          self->samp_rate / 2),
      goto fail);

  SU_TRYCATCH(
      suscli_param_read_float(
          p,
          "squelch_level",
          &self->squelch_level,
          .5),
      goto fail);

  SU_TRYCATCH(
      suscli_param_read_bool(
          p,
          "squelch",
          &self->squelch,
          SU_FALSE),
      goto fail);

  SU_TRYCATCH(
      suscli_param_read_bool(
          p,
          "disable_stderr",
          &self->disable_stderr,
          SU_TRUE),
      goto fail);

  suscli_radio_params_debug(self);

  suscan_source_config_set_freq(self->profile, self->frequency);

  ok = SU_TRUE;

fail:
  return ok;
}

/****************************** State handling *******************************/
SUPRIVATE void
suscli_radio_state_finalize(struct suscli_radio_state *self)
{
  if (self->player != NULL)
    suscli_audio_player_destroy(self->player);

  if (self->audio_data_samples != NULL)
    free(self->audio_data_samples);

  if (self->mutex_initialized)
    pthread_mutex_destroy(&self->mutex);

  memset(self, 0, sizeof (struct suscli_radio_state));
}

SUPRIVATE void
suscli_radio_state_mark_halting(struct suscli_radio_state *self)
{
  self->halting = SU_TRUE;
}

SUPRIVATE SUBOOL
suscli_radio_state_init(
    struct suscli_radio_state *state,
    const hashlist_t *params)
{
  SUBOOL ok = SU_FALSE;
  struct suscli_audio_player_params audio_params =
      suscli_audio_player_params_INITIALIZER;

  memset(state, 0, sizeof(struct suscli_radio_state));

  SU_TRYCATCH(suscli_radio_params_parse(&state->params, params), goto fail);

  SU_TRYCATCH(pthread_mutex_init(&state->mutex, NULL) != -1, goto fail);

  state->mutex_initialized = SU_TRUE;

  state->freq_step = 1e4;

  if (state->params.disable_stderr)
    freopen("/dev/null", "w", stderr);

  /* User requested audio play */
  audio_params.userdata = state;
  audio_params.start    = suscli_radio_audio_start_cb;
  audio_params.play     = suscli_radio_audio_play_cb;
  audio_params.stop     = suscli_radio_audio_stop_cb;
  audio_params.error    = suscli_radio_audio_error_cb;

  SU_TRYCATCH(
      state->player = suscli_audio_player_new(&audio_params),
      goto fail);

  ok = SU_TRUE;

fail:
  if (!ok)
    suscli_radio_state_finalize(state);

  return ok;
}

/************************** STDIN interface ***********************************/
SUPRIVATE SUBOOL
suscli_radio_helper_prepare_stdin(struct termios *old_termios)
{
  struct termios tty;

  if (tcgetattr(0, &tty) != 0)
    return SU_FALSE;

  *old_termios    = tty;

  tty.c_lflag    &= ~(ECHO | ICANON);

  if (tcsetattr(0, TCSANOW, &tty) != 0)
    return SU_FALSE;

  return SU_TRUE;
}

SUPRIVATE void
suscli_radio_state_parse_stdin_commands(struct suscli_radio_state *self)
{
  struct pollfd fd;
  int ret;
  char cmd;
  char freqbuffer[24];

  do {
    fd.fd = 0;
    fd.events = POLLIN;

    ret = poll(&fd, 1, 0);

    if (ret == 1 && read(0, &cmd, 1) == 1) {
      switch (cmd) {
        case 'a':
          self->frequency -= self->freq_step;
          suscli_chanloop_set_frequency(self->chanloop, self->frequency);
          suscli_radio_helper_format_frequency(
              self->frequency,
              freqbuffer,
              sizeof(freqbuffer));
          printf("\033[KTune to: %s\r", freqbuffer);
          fflush(stdout);
          break;

        case 'd':
          self->frequency += self->freq_step;
          suscli_chanloop_set_frequency(self->chanloop, self->frequency);
          suscli_radio_helper_format_frequency(
              self->frequency,
              freqbuffer,
              sizeof(freqbuffer));
          printf("\033[KTune to: %s\r", freqbuffer);
          fflush(stdout);
          break;

        case 'w':
          self->freq_step *= 10;
          suscli_radio_helper_format_frequency(
              self->freq_step,
              freqbuffer,
              sizeof(freqbuffer));
          printf("\033[KFrequency step: %s\r", freqbuffer);
          fflush(stdout);
          break;

        case 's':
          self->freq_step /= 10;
          suscli_radio_helper_format_frequency(
              self->freq_step,
              freqbuffer,
              sizeof(freqbuffer));
          printf("\033[KFrequency step: %s\r", freqbuffer);
          fflush(stdout);
          break;

        case 'm':
          self->params.demod = (self->params.demod) % 4 + 1;
          (void) suscan_config_set_integer(
              suscli_chanloop_get_config(self->chanloop),
              "audio.demodulator",
              self->params.demod);
          (void) suscli_chanloop_commit_config(self->chanloop);
          printf(
              "\033[KMode: %s\r",
              suscli_radio_demod_to_string(self->params.demod));
          fflush(stdout);
          break;
      }
    }
  } while (ret == 1);
}

/****************************** Capture ***************************************/
SUPRIVATE SUBOOL
suscli_radio_on_open_cb(
    suscan_analyzer_t *self,
    suscan_config_t *config,
    void *userdata)
{
  struct suscli_radio_state *state = (struct suscli_radio_state *) userdata;

  SU_TRYCATCH(
      suscan_config_set_float(
          config,
          "audio.volume",
          SU_MAG_RAW(state->params.volume_db)),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_config_set_float(
          config,
          "audio.cutoff",
          state->params.cutoff),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_config_set_integer(
          config,
          "audio.sample-rate",
          state->params.samp_rate),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_config_set_integer(
          config,
          "audio.demodulator",
          state->params.demod),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_config_set_bool(
          config,
          "audio.squelch",
          state->params.squelch),
      return SU_FALSE);

  SU_TRYCATCH(
      suscan_config_set_float(
          config,
          "audio.squelch-level",
          state->params.squelch_level),
      return SU_FALSE);

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscli_radio_on_data_cb(
    suscan_analyzer_t *self,
    const SUCOMPLEX *data,
    size_t size,
    void *userdata)
{
  SUCOMPLEX *tmp;
  SUSCOUNT new_alloc;
  struct suscli_radio_state *state = (struct suscli_radio_state *) userdata;

  pthread_mutex_lock(&state->mutex);

  if (state->audio_data_len + size > state->audio_data_alloc) {
    if (state->audio_data_alloc == 0)
      new_alloc = state->audio_data_len + size;
    else
      new_alloc = 2 * state->audio_data_alloc;

    SU_TRYCATCH(
        tmp = realloc(
            state->audio_data_samples,
            new_alloc * sizeof(SUCOMPLEX)),
        return SU_FALSE);

    state->audio_data_alloc   = new_alloc;
    state->audio_data_samples = tmp;
  }

  memcpy(
      state->audio_data_samples + state->audio_data_len,
      data,
      size * sizeof(SUCOMPLEX));

  state->audio_data_len += size;

  pthread_mutex_unlock(&state->mutex);

  if (state->got_termios)
    suscli_radio_state_parse_stdin_commands(state);

  if (state->halting) {
    SU_ERROR("Stopping capture.\n");
    return SU_FALSE;
  }

  return SU_TRUE;
}

void
suscli_radio_interrupt_handler(int sig)
{
  if (g_state != NULL) {
    if (g_state->params.disable_stderr)
      freopen("/dev/tty", "w", stderr);

    fprintf(stderr, "Ctrl+C hit, halting...\n");
    if (g_state->got_termios)
      tcsetattr(0, TCSANOW, &g_state->old_termios);
    g_state->halting = SU_TRUE;
    g_state = NULL;
  }
}

SUBOOL
suscli_radio_cb(const hashlist_t *params)
{
  suscli_chanloop_t *chanloop = NULL;
  struct suscli_chanloop_params chanloop_params =
      suscli_chanloop_params_INITIALIZER;
  struct suscli_radio_state state;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(suscli_radio_state_init(&state, params), goto fail);

  g_state = &state;
  signal(SIGINT, suscli_radio_interrupt_handler);

  chanloop_params.on_open  = suscli_radio_on_open_cb;
  chanloop_params.on_data  = suscli_radio_on_data_cb;
  chanloop_params.userdata = &state;

  chanloop_params.relbw = SU_ASFLOAT(5 * state.params.samp_rate) /
      suscan_source_config_get_samp_rate(state.params.profile);

  chanloop_params.rello = 0;
  chanloop_params.type  = "audio";

  SU_TRYCATCH(
      chanloop = suscli_chanloop_open(
          &chanloop_params,
          state.params.profile),
      goto fail);

  state.frequency = suscli_chanloop_get_freq(chanloop);
  state.chanloop = chanloop;
  state.got_termios = suscli_radio_helper_prepare_stdin(&state.old_termios);

  SU_TRYCATCH(suscli_chanloop_work(chanloop), goto fail);

  ok = SU_TRUE;

fail:
  suscli_radio_state_mark_halting(&state);

  if (chanloop != NULL)
    suscli_chanloop_destroy(chanloop);

  suscli_radio_state_finalize(&state);

  return ok;
}
