/*

  Copyright (C) 2020 Gonzalo José Carracedo Carballal

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation, version 3.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this program.  If not, see
  <http://www.gnu.org/licenses/>

*/

#define SU_LOG_DOMAIN "audio"

#include <sigutils/log.h>
#include <string.h>
#include <cli/audio.h>

#define SUSCLI_AUDIO_DEFAULT_SAMPLE_RATE 44100

#define SUSCLI_AUDIO_BUFFER_DELAY_MS     55
#define SUSCLI_AUDIO_MIN_BUFFER_SIZE     256
#define SUSCLI_AUDIO_BUFFER_ALLOC_SIZE   (5 * SUSCLI_AUDIO_DEFAULT_SAMPLE_RATE)

SUPRIVATE void *suscli_audio_open_stream(
  suscli_audio_player_t *,
  unsigned int,
  unsigned int);
SUPRIVATE void suscli_audio_close_stream(void *);
SUPRIVATE SUBOOL suscli_audio_play(void *, const SUFLOAT *, size_t);

/*************************** PortAudio implementation *************************/
#if defined(HAVE_ALSA)
#  include <alsa/asoundlib.h>
#  define HAVE_AUDIO
#  define ALSAPLAYER_UNDERRUN_WAIT_PERIOD_MS (2 * SUSCLI_AUDIO_BUFFER_DELAY_MS)

#define ATTEMPT(expr, what, arg...)                \
  if ((err = expr) < 0) {                          \
    fprintf(                                       \
      stderr,                                      \
      "Failed to " what  " in ALSA player: %s\n",  \
      ##arg,                                       \
      snd_strerror(err));                          \
    goto fail;                                     \
  }

SUPRIVATE void *
suscli_audio_open_stream(
    suscli_audio_player_t *self,
    unsigned int samp_rate,
    unsigned int buffer_size)
{
  int err;
  snd_pcm_t *pcm = NULL;
  snd_pcm_hw_params_t *params = NULL;
  const char *device = "default";

  ATTEMPT(
        snd_pcm_open(&pcm, device, SND_PCM_STREAM_PLAYBACK, 0),
        "open audio device %s",
        device);

  snd_pcm_hw_params_alloca(&params);
  snd_pcm_hw_params_any(pcm, params);

  ATTEMPT(
        snd_pcm_hw_params_set_access(
          pcm,
          params,
          SND_PCM_ACCESS_RW_INTERLEAVED),
        "set interleaved access for audio device");

  ATTEMPT(
        snd_pcm_hw_params_set_format(
          pcm,
          params,
          SND_PCM_FORMAT_FLOAT_LE),
        "set sample format");

  ATTEMPT(
        snd_pcm_hw_params_set_buffer_size(
          pcm,
          params,
          buffer_size),
        "set buffer size to %u",
        buffer_size);

  ATTEMPT(
        snd_pcm_hw_params_set_channels(pcm, params, 1),
        "set output to mono");

  ATTEMPT(
        snd_pcm_hw_params_set_rate_near(pcm, params, &samp_rate, NULL),
        "set sample rate to %u",
        samp_rate);

  ATTEMPT(snd_pcm_hw_params(pcm, params), "set device params");

  return pcm;

fail:
  if (pcm != NULL) {
    snd_pcm_drain(pcm);
    snd_pcm_close(pcm);
  }

  return NULL;
}

SUPRIVATE SUBOOL
suscli_audio_play(void *stream, const SUFLOAT *buffer, size_t len)
{
  long err;
  snd_pcm_t *pcm = (snd_pcm_t *) stream;

  err = snd_pcm_writei(pcm, buffer, len);

  if (err == -EPIPE) {
    usleep(ALSAPLAYER_UNDERRUN_WAIT_PERIOD_MS * 1000);
    snd_pcm_prepare(pcm);
    err = snd_pcm_writei(pcm, buffer, len);
  }

  return err >= 0;
}


SUPRIVATE void
suscli_audio_close_stream(void *stream)
{
  snd_pcm_t *pcm = (snd_pcm_t *) stream;

  snd_pcm_drain(pcm);
  snd_pcm_close(pcm);
}

#elif defined(HAVE_PORTAUDIO)
#  include <portaudio.h>
#  define HAVE_AUDIO
#  define PORTAUDIO_MAX_UNDERRUNS 20

SUPRIVATE SUBOOL pa_initialized = SU_FALSE;

SUPRIVATE void
pa_finalizer(void)
{
  Pa_Terminate();
}

SUPRIVATE SUBOOL
pa_assert_init(void)
{
  if (!pa_initialized) {
    PaError err = Pa_Initialize();
    pa_initialized = err == paNoError;

    if (pa_initialized)
      atexit(pa_finalizer);
  }

  return pa_initialized;
}

SUPRIVATE void *
suscli_audio_open_stream(
    suscli_audio_player_t *self,
    unsigned int samp_rate,
    unsigned int buffer_size)
{
  PaStreamParameters outputParameters;
  PaError pErr;
  void *stream = NULL;

  SU_TRYCATCH(pa_assert_init(), goto fail);

  if (Pa_GetDeviceInfo(Pa_GetDefaultOutputDevice()) == NULL) {
    SU_ERROR("No default PortAudio sound device found.\n");
    goto fail;
  }

  memset(&outputParameters, 0, sizeof (PaStreamParameters));

  outputParameters.device = Pa_GetDefaultOutputDevice(); /* default output device */
  outputParameters.channelCount = 1;
  outputParameters.sampleFormat = paFloat32;
  outputParameters.suggestedLatency =
      Pa_GetDeviceInfo(outputParameters.device)->defaultHighOutputLatency;
  outputParameters.hostApiSpecificStreamInfo = NULL;

  pErr = Pa_OpenStream(
     &stream,
     NULL,
     &outputParameters,
     samp_rate,
     buffer_size,
     paClipOff,
     NULL,
     NULL);

  if (pErr != paNoError) {
    SU_ERROR("Failed to open default sound device: %s\n", Pa_GetErrorText(pErr));
    goto fail;
  }

  pErr = Pa_StartStream(stream);

  if (pErr != paNoError) {
    SU_ERROR("Failed to start playback: %s\n", Pa_GetErrorText(pErr));
    suscli_audio_close_stream(stream);
    stream = NULL;
    goto fail;
  }

  self->samp_rate = samp_rate;

fail:
  return stream;
}

SUPRIVATE SUBOOL
suscli_audio_play(void *stream, const SUFLOAT *buffer, size_t len)
{
  PaError err;
  int i = 0;

  do {
    err = Pa_WriteStream(stream, buffer, len);
  } while (err == paOutputUnderflowed && i++ < PORTAUDIO_MAX_UNDERRUNS);

  if (err != paNoError)
    SU_ERROR("Portaudio error: %s\n", Pa_GetErrorText(err));

  return err == paNoError;
}


SUPRIVATE void
suscli_audio_close_stream(void *stream)
{
  Pa_StopStream(stream);
  Pa_CloseStream(stream);
}
#endif /* HAVE_PORTAUDIO */

#ifndef HAVE_AUDIO
SUPRIVATE void *
suscli_audio_open_stream(
  suscli_audio_player_t *self,
  unsigned int samp_rate,
  unsigned int bufsiz)
{
  SU_ERROR("Audio support disabled at compile time.\n");
  return NULL;
}

SUPRIVATE SUBOOL
suscli_audio_play(void *stream, const SUFLOAT *buffer, size_t len)
{
  return SU_FALSE;
}


SUPRIVATE void
suscli_audio_close_stream(void *stream)
{

}
#endif /* HAVE_AUDIO */

/**************************** Suscli Audio playback ***************************/
SUBOOL
suscli_audio_playback_cb(
    struct suscan_mq *mq_out,
    void *wk_private,
    void *cb_private)
{
  suscli_audio_player_t *player = (suscli_audio_player_t *) wk_private;
  size_t size = player->bufsiz;
  SUBOOL ok = SU_FALSE;

  if (!(player->params.play) (
          player,
          player->buffer,
          &size,
          player->params.userdata)) {
    goto fail;
  }

  if (size > 0) {
    SU_TRYCATCH(
        suscli_audio_play(player->stream, player->buffer, size),
        goto fail);
  }

  ok = SU_TRUE;

fail:
  if (!ok) {
    if (player->params.error != NULL)
      (player->params.error) (player, player->params.userdata);
    player->failed = SU_TRUE;
  }
  return ok;
}

suscli_audio_player_t *
suscli_audio_player_new(const struct suscli_audio_player_params *params)
{
  suscli_audio_player_t *new = NULL;

  SU_TRYCATCH(params->play != NULL, goto fail);

  SU_TRYCATCH(new = calloc(1, sizeof(suscli_audio_player_t)), goto fail);

  if (new->params.samp_rate == 0)
    new->params.samp_rate = SUSCLI_AUDIO_DEFAULT_SAMPLE_RATE;

  new->params   = *params;
  new->bufsiz   = SUSCLI_AUDIO_BUFFER_DELAY_MS * 1e-3f * new->params.samp_rate;
  new->bufalloc = SUSCLI_AUDIO_BUFFER_ALLOC_SIZE;

  if (new->bufsiz < SUSCLI_AUDIO_MIN_BUFFER_SIZE)
    new->bufsiz = SUSCLI_AUDIO_MIN_BUFFER_SIZE;
  else if (new->bufsiz > SUSCLI_AUDIO_BUFFER_ALLOC_SIZE)
    new->bufsiz = SUSCLI_AUDIO_BUFFER_ALLOC_SIZE;

  SU_TRYCATCH(
      new->buffer = calloc(new->bufalloc, sizeof(SUFLOAT)),
      goto fail);

  SU_TRYCATCH(suscan_mq_init(&new->mq), goto fail);
  SU_TRYCATCH(new->worker = suscan_worker_new(&new->mq, new), goto fail);

  SU_TRYCATCH(
      new->stream = suscli_audio_open_stream(new, params->samp_rate, new->bufsiz),
      goto fail);

  if (new->params.start != NULL)
    (new->params.start) (new, new->params.userdata);

  /* Go, go, go! */
  SU_TRYCATCH(
      suscan_worker_push(
          new->worker,
          suscli_audio_playback_cb,
          params->userdata),
      goto fail);

  return new;

fail:
  if (new != NULL)
    suscli_audio_player_destroy(new);

  return NULL;
}

void *
suscli_audio_player_wait(suscli_audio_player_t *self, uint32_t *type)
{
  return suscan_mq_read(&self->mq, type);
}

void
suscli_audio_player_destroy(suscli_audio_player_t *self)
{
  if (self->worker != NULL)
    suscan_worker_halt(self->worker);

  if (self->params.stop != NULL)
    (self->params.stop) (self, self->params.userdata);

  if (self->stream != NULL)
    suscli_audio_close_stream(self->stream);

  if (self->buffer != NULL)
    free(self->buffer);

  suscan_mq_finalize(&self->mq);

  free(self);
}
