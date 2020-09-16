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

#ifndef _CLI_CHANLOOP_H
#define _CLI_CHANLOOP_H

#include <analyzer/analyzer.h>

struct suscli_chanloop_params {
  SUFLOAT relbw;
  SUFLOAT rello;
  const char *type;
  void *userdata;
  SUBOOL (*on_data) (suscan_analyzer_t *, const SUCOMPLEX *, size_t, void *);
  SUBOOL (*on_open) (suscan_analyzer_t *, suscan_config_t *, void *);
};

#define suscli_chanloop_params_INITIALIZER      \
{                                               \
  .25,  /* relbw */                             \
  .25,  /* rello */                             \
  NULL, /* type */                              \
  NULL, /* userdata */                          \
  NULL, /* on_data */                           \
}

struct suscli_chanloop {
  struct suscli_chanloop_params params;
  suscan_analyzer_t *analyzer;
  struct suscan_mq mq;
  suscan_config_t *inspcfg;
  SUFLOAT equiv_fs;
  SUFREQ  ft;
};

typedef struct suscli_chanloop suscli_chanloop_t;

suscli_chanloop_t *suscli_chanloop_open(
    const struct suscli_chanloop_params *params,
    suscan_source_config_t *cfg);

SUBOOL suscli_chanloop_work(suscli_chanloop_t *self);

SUBOOL suscli_chanloop_cancel(suscli_chanloop_t *self);

SUBOOL suscli_chanloop_set_rate(suscli_chanloop_t *self, SUFLOAT);

void suscli_chanloop_destroy(suscli_chanloop_t *self);

SUINLINE SUFREQ
suscli_chanloop_get_freq(const suscli_chanloop_t *self)
{
  return self->ft;
}

SUINLINE SUFREQ
suscli_chanloop_get_equiv_fs(const suscli_chanloop_t *self)
{
  return self->equiv_fs;
}

SUINLINE suscan_config_t *
suscli_chanloop_get_config(const suscli_chanloop_t *self)
{
  return self->inspcfg;
}

#endif /* _CLI_CHANLOOP_H */

