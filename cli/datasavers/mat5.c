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

#define SU_LOG_DOMAIN "mat5-datasaver"

#include <sigutils/log.h>
#include <cli/datasaver.h>
#include <cli/cli.h>
#include <errno.h>
#include <time.h>
#include <string.h>
#include <sigutils/matfile.h>

SUPRIVATE su_mat_file_t *
suscli_mat5_fopen(const char *path)
{
  su_mat_file_t *mf = NULL;
  su_mat_matrix_t *mtx = NULL;
  char *new_path = NULL;
  time_t now;
  struct tm tm;
  SUBOOL ok = SU_FALSE;

  if (path == NULL || strlen(path) == 0) {
    time(&now);
    gmtime_r(&now, &tm);

    SU_TRYCATCH(
        new_path = strbuild(
            "capture_%04d%02d%02d_%02d%02d%02d.mat",
            tm.tm_year + 1900,
            tm.tm_mon + 1,
            tm.tm_mday,
            tm.tm_hour,
            tm.tm_min,
            tm.tm_sec),
        goto fail);
    path = new_path;
  }

  SU_TRYCATCH(mf = su_mat_file_new(), goto fail);
  SU_TRYCATCH(mtx = su_mat_file_make_matrix(mf, "XT0", 1, 1), goto fail);
  SU_TRYCATCH(su_mat_matrix_write_col(mtx, SU_ASFLOAT(now)), goto fail);
  SU_TRYCATCH(su_mat_file_make_streaming_matrix(mf, "X", 4, 0), goto fail);
  SU_TRYCATCH(su_mat_file_get_matrix_by_handle(mf, 0) == mtx, goto fail);
  SU_TRYCATCH(su_mat_file_dump(mf, path), goto fail);

  ok = SU_TRUE;

fail:
  if (new_path != NULL)
    free(new_path);

  if (!ok && mf != NULL) {
    su_mat_file_destroy(mf);
    mf = NULL;
  }

  return mf;
}

SUPRIVATE void *
suscli_mat5_datasaver_open_cb(void *userdata)
{
  const char *path = NULL;
  const hashlist_t *params = (const hashlist_t *) userdata;

  SU_TRYCATCH(
      suscli_param_read_string(params, "path", &path, NULL),
      return NULL);

  return suscli_mat5_fopen(path);
}

SUPRIVATE SUBOOL
suscli_mat5_datasaver_write_cb(
    void *state,
    const struct suscli_sample *samples,
    size_t length)
{
  su_mat_file_t *mf = (su_mat_file_t *) state;
  unsigned long T0;
  int i;

  T0 = (unsigned long) su_mat_matrix_get(
      su_mat_file_get_matrix_by_handle(mf, 0), 0, 0);

  for (i = 0; i < length; ++i) {
    SU_TRYCATCH(
        su_mat_file_stream_col(
            mf,
            SU_ASFLOAT(samples[i].timestamp.tv_sec - T0),
            SU_ASFLOAT(samples[i].timestamp.tv_usec * 1e-6),
            samples[i].value,
            SU_POWER_DB_RAW(samples[i].value)),
        return SU_FALSE);
  }

  su_mat_file_flush(mf);

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscli_mat5_datasaver_close_cb(void *state)
{
  su_mat_file_t *mf = (su_mat_file_t *) state;

  su_mat_file_destroy(mf);

  return SU_TRUE;
}

void
suscli_datasaver_params_init_mat5(
    struct suscli_datasaver_params *self,
    const hashlist_t *params) {
  self->userdata = (void *) params;
  self->open  = suscli_mat5_datasaver_open_cb;
  self->write = suscli_mat5_datasaver_write_cb;
  self->close = suscli_mat5_datasaver_close_cb;
}
