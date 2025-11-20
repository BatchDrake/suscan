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

#define SU_LOG_DOMAIN "cli-spectrum"

#include <sigutils/log.h>
#include <sigutils/sampling.h>

#include <analyzer/analyzer.h>
#include <analyzer/source.h>
#include <analyzer/msg.h>
#include <signal.h>
#include <time.h>
#include <sys/stat.h>

#include <cli/cli.h>
#include <cli/cmds.h>

#include <util/npy.h>

/***************************** Spectrum integrator ****************************/
enum suscli_vector_integrator_type {
  SUSCLI_SPECTRUM_INTEGRATOR_LINEAR,
  SUSCLI_SPECTRUM_INTEGRATOR_LOG,
  SUSCLI_SPECTRUM_INTEGRATOR_MAX,
};

struct suscli_vector_integrator {
  enum suscli_vector_integrator_type type;
  uint64_t length;
  uint64_t count;
  uint64_t n;
  uint64_t alloc;
  SUFLOAT  Kinv;
  SUFLOAT *psd_int;
  SUFLOAT *psd_com;
};

typedef struct suscli_vector_integrator suscli_vector_integrator_t;

SU_COLLECTOR(suscli_vector_integrator)
{
  if (self->psd_int != NULL)
    free(self->psd_int);

  if (self->psd_com != NULL)
    free(self->psd_com);

  free(self);
}

SU_INSTANCER(
  suscli_vector_integrator,
  enum suscli_vector_integrator_type type,
  uint64_t bins,
  uint64_t count)
{
  suscli_vector_integrator_t *new = NULL;

  SU_ALLOCATE_FAIL(new, suscli_vector_integrator_t);

  if (count <= 1) {
    type  = SUSCLI_SPECTRUM_INTEGRATOR_MAX;
    count = 1;
  }

  new->type   = type;
  new->length = bins;
  new->Kinv   = 1. / count;
  new->count  = count;
  new->alloc  = bins * sizeof(SUFLOAT);

  SU_ALLOCATE_MANY_FAIL(new->psd_int, bins, SUFLOAT);

  if (type != SUSCLI_SPECTRUM_INTEGRATOR_MAX)
    SU_ALLOCATE_MANY_FAIL(new->psd_com,  bins, SUFLOAT);

  return new;

fail:
  if (new != NULL)
    SU_DISPOSE(suscli_vector_integrator, new);

  return NULL;
}

SUINLINE
SU_METHOD(suscli_vector_integrator, SUBOOL, feed, const SUFLOAT *data)
{
  SUBOOL ready;
  uint64_t i;
  const uint64_t len = self->length;

  if (self->n == 0) {
    /* First spectrum. Just copy the data. */
    memcpy(self->psd_int, data, self->alloc);
  } else if (self->n < self->count) {
    /* Next spectra: accumulate via Kahan */
    volatile SUFLOAT y, t, c, S;

    switch (self->type) {
      case SUSCLI_SPECTRUM_INTEGRATOR_LINEAR:
        for (i = 0; i < len; ++i) {
          S = self->psd_int[i];
          c = self->psd_com[i];
          
          y = data[i] - c;
          t = S + y;

          self->psd_int[i] = t;
          self->psd_com[i]  = (t - S) - y;
        }
        break;

      case SUSCLI_SPECTRUM_INTEGRATOR_LOG:
        for (i = 0; i < len; ++i) {
          S = self->psd_int[i];
          c = self->psd_com[i];
          
          y = SU_LN(data[i]) - c;
          t = S + y;

          self->psd_int[i] = t;
          self->psd_com[i]  = (t - S) - y;
        }
        break;
      
      case SUSCLI_SPECTRUM_INTEGRATOR_MAX:
        for (i = 0; i < len; ++i)
          if (data[i] > self->psd_int[i])
            self->psd_int[i] = data[i];
        break;
    }
  } else {
    SU_WARNING("Skipping spectrum (unrecovered integration product available)\n");
    return SU_TRUE;
  }

  ++self->n;

  ready = self->n == self->count;

  if (ready) {
    switch (self->type) {
      case SUSCLI_SPECTRUM_INTEGRATOR_LINEAR:
        for (i = 0; i < len; ++i) {
          self->psd_int[i] *= self->Kinv;
          self->psd_com[i]  = 0;
        }
        break;

      case SUSCLI_SPECTRUM_INTEGRATOR_LOG:
        for (i = 0; i < len; ++i) {
          self->psd_int[i] = SU_EXP(self->Kinv * self->psd_int[i]);
          self->psd_com[i] = 0;
        }
        break;

      case SUSCLI_SPECTRUM_INTEGRATOR_MAX:
        /* No-op */
        break;
    }
  }

  return ready;
}

SUINLINE
SU_METHOD(suscli_vector_integrator, const SUFLOAT *, take)
{
  if (self->n == self->count) {
    self->n = 0;
    return self->psd_int;
  }

  return NULL;
}

/******************************* Spectrum state *******************************/
struct suscli_spectrum_params {
  suscan_source_config_t              *profile;
  SUFLOAT                              psd_rate;
  uint32_t                             fft_size;
  uint64_t                             fft_num;
  uint64_t                             fft_per_dataset;
  enum suscli_vector_integrator_type   integrator;
  const char                          *prefix;
};

#define suscli_spectrum_params_INITIALIZER {  \
  NULL,  /* profile  */                       \
  25,    /* psd_rate */                       \
  8192,  /* fft_size */                       \
  10,    /* fft_num  */                       \
  10,    /* fft_per_dataset */                \
  SUSCLI_SPECTRUM_INTEGRATOR_LINEAR,          \
  NULL,  /* prefix */                         \
}

enum suscli_spectrum_state {
  SUSCLI_SPECTRUM_STARTUP,
  SUSCLI_SPECTRUM_CONFIGURING,
  SUSCLI_SPECTRUM_ACQUIRING
};


struct suscli_spectrum {
  struct suscli_spectrum_params params;
  suscan_analyzer_t            *analyzer;
  struct suscan_analyzer_params analyzer_params;
  enum suscli_spectrum_state    state;
  char                         *prefix;
  SUFLOAT                       samp_rate;
  SUFLOAT                       psd_rate;

  /*
   * Analyzer state
   */
  SUBOOL                        have_source_info;
  SUBOOL                        have_analyzer_params;

  /* 
   * Output files of the current dataset
   */
  uint32_t                      dataset;
  uint64_t                      count;
  char                         *odir;
  char                         *ts_path;
  FILE                         *ts_fp;
  npy_file_t                   *ts_npy;

  char                         *sdata_path;
  FILE                         *sdata_fp;
  npy_file_t                   *sdata_npy;
  
  /*
   * Integration state
   */
  suscli_vector_integrator_t *integrator;
};

typedef struct suscli_spectrum suscli_spectrum_t;

SU_METHOD(suscli_spectrum, void, close_dataset)
{
  if (self->ts_npy != NULL) {
    SU_DISPOSE(npy_file, self->ts_npy);
    self->ts_npy = NULL;
  }

  if (self->sdata_npy != NULL) {
    SU_DISPOSE(npy_file, self->sdata_npy);
    self->sdata_npy = NULL;
  }

  if (self->ts_fp != NULL) {
    fclose(self->ts_fp);
    self->ts_fp = NULL;
  }

  if (self->sdata_fp != NULL) {
    fclose(self->sdata_fp);
    self->sdata_fp = NULL;
  }
  
  if (self->odir != NULL) {
    free(self->odir);
    self->odir = NULL;
  }

  if (self->ts_path != NULL) {
    free(self->ts_path);
    self->ts_path = NULL;
  }

  if (self->sdata_path != NULL) {
    free(self->sdata_path);
    self->sdata_path = NULL;
  }

  self->count = 0;
}

SU_COLLECTOR(suscli_spectrum)
{
  suscli_spectrum_close_dataset(self);

  if (self->integrator != NULL)
    SU_DISPOSE(suscli_vector_integrator, self->integrator);
  
  if (self->prefix != NULL)
    free(self->prefix);
  
  free(self);
}

SU_METHOD(suscli_spectrum, SUBOOL, open_dataset)
{
  SUBOOL ok = SU_FALSE;
  char *tmp = NULL;
  uint32_t dataset = self->dataset + 1;

  SU_TRY(self->odir = strbuild("%s_%05d", self->prefix, dataset));
  
  if (access(self->odir, F_OK) != -1) {
    SU_ERROR("Cannot create output directory %s: file exists\n", self->odir);
    goto done;
  }

  if (mkdir(self->odir, 0755) == -1) {
    SU_ERROR(
      "Cannot create output directory %s: %s\n",
      self->odir,
      strerror(errno));
    goto done;
  }

  SU_TRY(self->ts_path    = strbuild("%s/ts.npy",    self->odir));
  SU_TRY(self->sdata_path = strbuild("%s/sdata.npy", self->odir));

  SU_TRY(tmp = strbuild("%s/samp_rate.npy", self->odir));
  SU_TRY(npy_file_store_float32(tmp, &self->samp_rate, 1));

  if ((self->ts_fp = fopen(self->ts_path, "wb")) == NULL) {
    SU_ERROR("Cannot open %s for writing: %s\n", self->ts_path, strerror(errno));
    goto done;
  }

  SU_MAKE(self->ts_npy, npy_file, self->ts_fp, NPY_DTYPE_INT32, 1, 2, 0);

  if ((self->sdata_fp = fopen(self->sdata_path, "wb")) == NULL) {
    SU_ERROR("Cannot open %s for writing: %s\n", self->sdata_path, strerror(errno));
    goto done;
  }

  SU_MAKE(
    self->sdata_npy,
    npy_file,
    self->sdata_fp, NPY_DTYPE_FLOAT32, 1, self->params.fft_size, 0);
  
  self->dataset = dataset;
  
  ok = SU_TRUE;

done:
  if (tmp != NULL)
    free(tmp);
  
  return ok;  
}

SU_INSTANCER(
  suscli_spectrum,
  suscan_analyzer_t *analyzer,
  const struct suscli_spectrum_params *params)
{
  suscli_spectrum_t *new = NULL;
  struct timeval tv;
  struct tm tm;

  SU_ALLOCATE_FAIL(new, suscli_spectrum_t);

  new->params   = *params;
  new->analyzer = analyzer;
  new->state    = SUSCLI_SPECTRUM_STARTUP;
  new->psd_rate = 25;

  if (params->prefix == NULL) {
    gettimeofday(&tv, NULL);
    gmtime_r(&tv.tv_sec, &tm);

    SU_TRY_FAIL(new->prefix =
      strbuild(
        "%04d%02d%02d_%02d%02d%02dZ",
        tm.tm_year + 1900,
        tm.tm_mon + 1,
        tm.tm_mday,
        tm.tm_hour,
        tm.tm_min,
        tm.tm_sec));  
  } else {
    SU_TRY_FAIL(new->prefix = strdup(params->prefix));
  }

  SU_MAKE_FAIL(
    new->integrator,
    suscli_vector_integrator,
    params->integrator, params->fft_size, params->fft_num);

  return new;

fail:
  if (new != NULL)
    SU_DISPOSE(suscli_spectrum, new);

  return NULL;
}

SUPRIVATE SUBOOL g_halting = SU_FALSE;

void
suscli_spectrum_int_handler(int sig)
{
  g_halting = SU_TRUE;
}

SUPRIVATE SUBOOL
suscli_spectrum_msg_is_final(uint32_t type)
{
  return 
       (type == SUSCAN_ANALYZER_MESSAGE_TYPE_EOS)
    || (type == SUSCAN_ANALYZER_MESSAGE_TYPE_READ_ERROR)
    || (type == SUSCAN_WORKER_MSG_TYPE_HALT);
}


/*
 * Startup state: wait for source info and analyzer params to decide how to
 * configure the PSD rate.
 */

SUPRIVATE SUBOOL
suscli_spectrum_process_startup_message(
  struct suscli_spectrum *self,
  const struct suscan_msg *msg)
{
  struct suscan_source_info *info;
  struct suscan_analyzer_params *params;
  SUBOOL ok = SU_FALSE;

  switch (msg->type) {
    case SUSCAN_ANALYZER_MESSAGE_TYPE_SOURCE_INFO:
      info                   = msg->privdata;
      self->samp_rate        = info->source_samp_rate;
      self->have_source_info = SU_TRUE;
      break;

    case SUSCAN_ANALYZER_MESSAGE_TYPE_PARAMS:
      params                     = msg->privdata;
      self->psd_rate             = 1. / params->psd_update_int;
      self->analyzer_params      = *params;
      self->have_analyzer_params = SU_TRUE;
      break;
  }

  if (self->have_source_info && self->have_analyzer_params) {
    SU_INFO("  Source sample rate: %g sps\n", self->samp_rate);
    SU_INFO("  PSD refresh rate:   %g fps\n", self->psd_rate);
    SU_INFO("Entering in configuring state...\n");

    self->analyzer_params.psd_update_int              = 1 / self->params.psd_rate;
    self->analyzer_params.detector_params.window_size = self->params.fft_size;

    self->state = SUSCLI_SPECTRUM_CONFIGURING;

    SU_TRY(
      suscan_analyzer_set_params_async(
        self->analyzer,
        &self->analyzer_params,
        0));
  }

  ok = SU_TRUE;

done:
  return ok;
}

SUPRIVATE SUBOOL
suscli_spectrum_process_configuring_message(
  struct suscli_spectrum *self,
  const struct suscan_msg *msg)
{
  if (msg->type == SUSCAN_ANALYZER_MESSAGE_TYPE_PARAMS) {
    struct suscan_analyzer_params *params;

    params                     = msg->privdata;
    self->psd_rate             = 1. / params->psd_update_int;
    self->analyzer_params      = *params;

    SU_INFO("  PSD refresh rate (configured): %g fps\n",  self->psd_rate);
    SU_INFO("  PSD FFT size     (configured): %d bins\n", params->detector_params.window_size);

    if (params->detector_params.window_size != self->params.fft_size) {
      SU_ERROR("Analyzer rejected our FFT size. Refusing to continue\n");
      return SU_FALSE;
    }

    self->state = SUSCLI_SPECTRUM_ACQUIRING;
  }

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscli_spectrum_process_acquiring_message(
  struct suscli_spectrum *self,
  const struct suscan_msg *msg)
{
  SUBOOL ok = SU_FALSE;
  SUBOOL new_dataset = SU_FALSE;

  if (msg->type == SUSCAN_ANALYZER_MESSAGE_TYPE_PSD) {
    struct suscan_analyzer_psd_msg *psd = msg->privdata;

    if (suscli_vector_integrator_feed(self->integrator, psd->psd_data)) {
      int32_t ts[2] = {psd->timestamp.tv_sec, psd->timestamp.tv_usec};

      if (self->dataset == 0) {
        SU_TRY(suscli_spectrum_open_dataset(self));
        new_dataset = SU_TRUE;
      }

      SU_TRY(npy_file_write_int32(self->ts_npy, ts, 2));
      SU_TRY(
        npy_file_write_float32(
          self->sdata_npy,
          suscli_vector_integrator_take(self->integrator),
          self->params.fft_size));
      
      if (++self->count == self->params.fft_per_dataset) {
        suscli_spectrum_close_dataset(self);
        SU_TRY(suscli_spectrum_open_dataset(self));
        new_dataset = SU_TRUE;
      }

      if (new_dataset)
        SU_INFO("Dataset changed: %s\n", self->odir);
    }
  }

  ok = SU_TRUE;

done:
  return ok;
}

SUPRIVATE SUBOOL
suscli_spectrum_process_message(
  struct suscli_spectrum *self,
  const struct suscan_msg *msg)
{
  SUBOOL ok = SU_FALSE;

  switch (self->state) {
    case SUSCLI_SPECTRUM_STARTUP:
      SU_TRY(suscli_spectrum_process_startup_message(self, msg));
      break;

    case SUSCLI_SPECTRUM_CONFIGURING:
      SU_TRY(suscli_spectrum_process_configuring_message(self, msg));
      break;

    case SUSCLI_SPECTRUM_ACQUIRING:
      SU_TRY(suscli_spectrum_process_acquiring_message(self, msg));
      break;
  }

  ok = SU_TRUE;

done:
  return ok;
}

SUBOOL
suscli_spectrum_cb(const hashlist_t *params)
{
  SUBOOL ok = SU_FALSE;
  suscli_spectrum_t *spectrum = NULL;
  suscan_analyzer_t *analyzer = NULL;
  struct suscli_spectrum_params sparm = suscli_spectrum_params_INITIALIZER;
  struct suscan_analyzer_params aparm = suscan_analyzer_params_INITIALIZER;
  struct suscan_mq omq;
  struct suscan_msg *msg = NULL;
  struct timeval tv;

  SU_TRY(suscan_mq_init(&omq));
  SU_TRY(suscli_param_read_profile(params, "profile", &sparm.profile));

  SU_MAKE(analyzer, suscan_analyzer, &aparm, sparm.profile, &omq);
  SU_MAKE(spectrum, suscli_spectrum, analyzer, &sparm);

  signal(SIGINT, suscli_spectrum_int_handler);

  while (!g_halting) {
    tv.tv_sec  = 0;
    tv.tv_usec = 100000;
    msg = suscan_mq_read_msg_timeout(&omq, &tv);

    if (msg != NULL) {
      if (suscli_spectrum_msg_is_final(msg->type))
        g_halting = SU_TRUE;

      SU_TRY(suscli_spectrum_process_message(spectrum, msg));

      suscan_analyzer_dispose_message(msg->type, msg->privdata);
      SU_DISPOSE(suscan_msg, msg);
      msg = NULL;
    }
  }

  ok = SU_TRUE;

done:
  if (msg != NULL) {
    suscan_analyzer_dispose_message(msg->type, msg->privdata);
    SU_DISPOSE(suscan_msg, msg);
  }
  
  if (spectrum != NULL) {
    SU_INFO("Flushing spectrum data...\n");
    SU_DISPOSE(suscli_spectrum, spectrum);
  }

  if (analyzer != NULL)
    SU_DISPOSE(suscan_analyzer, analyzer);

  suscan_mq_finalize(&omq);

  return ok;
}

