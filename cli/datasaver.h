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

#ifndef _SUSCLI_DATASAVER_H
#define _SUSCLI_DATASAVER_H

#include <sigutils/types.h>
#include <analyzer/analyzer.h>
#include <hashlist.h>
#include <sys/time.h>
#include <pthread.h>

#define SUSCLI_DATASAVER_BLOCK_SIZE 4096

struct suscli_sample {
  struct timeval timestamp;
  SUFLOAT value;
};

struct suscli_datasaver_params {
  void *userdata;
  void *(*open) (void *userdata);
  SUBOOL (*write) (void *state, const struct suscli_sample *, size_t);
  SUBOOL (*close) (void *state);
};

struct suscli_datasaver {
  struct suscli_datasaver_params params;
  SUBOOL failed;
  void *state;
  SUBOOL have_mq;
  SUBOOL have_mutex;
  suscan_worker_t *worker;
  struct suscan_mq mq;
  pthread_mutex_t mutex;
  size_t block_size;
  size_t block_consumed;
  size_t block_ptr;
  struct suscli_sample *block_buffer;
};

typedef struct suscli_datasaver suscli_datasaver_t;

suscli_datasaver_t *suscli_datasaver_new(
    const struct suscli_datasaver_params *);

SUBOOL suscli_datasaver_write(suscli_datasaver_t *, SUFLOAT);

void suscli_datasaver_destroy(suscli_datasaver_t *);

/***************************** Implementations ********************************/
void suscli_datasaver_params_init_matlab(
    struct suscli_datasaver_params *self,
    const hashlist_t *params);
void suscli_datasaver_params_init_mat5(
    struct suscli_datasaver_params *self,
    const hashlist_t *params);
void suscli_datasaver_params_init_csv(
    struct suscli_datasaver_params *self,
    const hashlist_t *params);
void suscli_datasaver_params_init_tcp(
    struct suscli_datasaver_params *self,
    const hashlist_t *params);

#endif /* _SUSCLI_DATASAVER_H */
