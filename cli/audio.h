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

#ifndef _CLI_AUDIO_H
#define _CLI_AUDIO_H

#include <sigutils/types.h>
#include <analyzer/worker.h>
#include <pthread.h>

struct suscli_audio_player;

struct suscli_audio_player_params {
  void *userdata;
  unsigned int samp_rate;
  SUBOOL (*start) (struct suscli_audio_player *, void *userdata);
  SUBOOL (*play)  (struct suscli_audio_player *, SUFLOAT *, size_t *, void *);
  void   (*stop)  (struct suscli_audio_player *, void *userdata);
  void   (*error) (struct suscli_audio_player *, void *userdata);
};

#define suscli_audio_player_params_INITIALIZER { \
  NULL, /* userdata */                           \
  0,    /* samp_rate */                          \
  NULL, /* start */                              \
  NULL, /* play */                               \
  NULL, /* stop */                               \
  NULL, /* error */                              \
}

struct suscli_audio_player {
  struct suscli_audio_player_params params;
  suscan_worker_t *worker;
  struct suscan_mq mq;
  unsigned int samp_rate;
  SUBOOL   failed;
  SUFLOAT *buffer;
  size_t   bufsiz;
  size_t   bufalloc;

  void    *stream;
};

typedef struct suscli_audio_player suscli_audio_player_t;

SUINLINE unsigned int
suscli_audio_player_samp_rate(const suscli_audio_player_t *self)
{
  return self->samp_rate;
}

SUINLINE SUBOOL
suscli_audio_player_failed(const suscli_audio_player_t *self)
{
  return self->failed;
}

SUINLINE size_t
suscli_audio_player_get_buffer_alloc_size(const suscli_audio_player_t *self)
{
  return self->bufalloc;
}

suscli_audio_player_t *suscli_audio_player_new(
    const struct suscli_audio_player_params *params);

void *suscli_audio_player_wait(suscli_audio_player_t *self, uint32_t *type);

void suscli_audio_player_destroy(suscli_audio_player_t *self);

#endif /* _CLI_AUDIO_H */

