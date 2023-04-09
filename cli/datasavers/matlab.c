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

#define SU_LOG_DOMAIN "matlab-datasaver"

#include <sigutils/log.h>
#include <cli/datasaver.h>
#include <cli/cli.h>
#include <errno.h>
#include <util/compat-time.h>
#include <string.h>

SUPRIVATE char *
suscli_matlab_datasaver_fname_cb(void)
{
  time_t now;
  struct tm tm;

  time(&now);
  gmtime_r(&now, &tm);

  return strbuild(
            "capture_%04d%02d%02d_%02d%02d%02d.m",
            tm.tm_year + 1900,
            tm.tm_mon + 1,
            tm.tm_mday,
            tm.tm_hour,
            tm.tm_min,
            tm.tm_sec);
}

SUPRIVATE FILE *
suscli_matlab_fopen(const char *path)
{
  FILE *fp = NULL;
  char *new_path = NULL;
  SUBOOL ok = SU_FALSE;

  if (path == NULL || strlen(path) == 0) {
    SU_TRYCATCH(new_path = suscli_matlab_datasaver_fname_cb(), goto fail);
    path = new_path;
  }

  if ((fp = fopen(path, "w")) == NULL) {
    SU_ERROR("Cannot open `%s' for writing: %s\n", path, strerror(errno));
    goto fail;
  }

  SU_TRYCATCH(fprintf(fp, "X = [\n") > 0, goto fail);

  ok = SU_TRUE;

fail:
  if (new_path != NULL)
    free(new_path);

  if (!ok && fp != NULL) {
    fclose(fp);
    fp = NULL;
  }

  return fp;
}

SUPRIVATE void *
suscli_matlab_datasaver_open_cb(void *userdata)
{
  const char *path = NULL;
  const hashlist_t *params = (const hashlist_t *) userdata;

  SU_TRYCATCH(
      suscli_param_read_string(params, "path", &path, NULL),
      return NULL);

  return suscli_matlab_fopen(path);
}

SUPRIVATE SUBOOL
suscli_matlab_datasaver_write_cb(
    void *state,
    const struct suscli_sample *samples,
    size_t length)
{
  FILE *fp = (FILE *) state;
  int i;

  for (i = 0; i < length; ++i) {
    SU_TRYCATCH(
        fprintf(
            fp,
            "  %ld,%.6lf,%.9e,%g;\n",
            samples[i].timestamp.tv_sec,
            samples[i].timestamp.tv_usec * 1e-6,
            samples[i].value,
            SU_POWER_DB_RAW(samples[i].value)) > 0,
        return SU_FALSE);
  }

  fflush(fp);
  
  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscli_matlab_datasaver_close_cb(void *state)
{
  FILE *fp = (FILE *) state;

  fprintf(fp, "];\n");
  fclose(fp);

  return SU_TRUE;
}

void
suscli_datasaver_params_init_matlab(
    struct suscli_datasaver_params *self,
    const hashlist_t *params) {
  self->userdata = (void *) params;
  self->fname = suscli_matlab_datasaver_fname_cb;
  self->open  = suscli_matlab_datasaver_open_cb;
  self->write = suscli_matlab_datasaver_write_cb;
  self->close = suscli_matlab_datasaver_close_cb;
}
