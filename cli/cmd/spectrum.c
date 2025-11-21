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
#include <sigutils/detect.h>
#include <string.h>

#include <analyzer/analyzer.h>
#include <analyzer/source.h>
#include <analyzer/msg.h>
#include <signal.h>
#include <time.h>
#include <sys/stat.h>

#include <cli/cli.h>
#include <cli/cmds.h>
#include <ctype.h>

#include <util/npy.h>
#include <util/units.h>

/***************************** Spectrum integrator ****************************/
enum suscli_vector_integrator_type {
  SUSCLI_VECTOR_INTEGRATOR_LINEAR,
  SUSCLI_VECTOR_INTEGRATOR_LOG,
  SUSCLI_VECTOR_INTEGRATOR_MAX,
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
    type  = SUSCLI_VECTOR_INTEGRATOR_MAX;
    count = 1;
  }

  new->type   = type;
  new->length = bins;
  new->Kinv   = 1. / count;
  new->count  = count;
  new->alloc  = bins * sizeof(SUFLOAT);

  SU_ALLOCATE_MANY_FAIL(new->psd_int, bins, SUFLOAT);

  if (type != SUSCLI_VECTOR_INTEGRATOR_MAX)
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
      case SUSCLI_VECTOR_INTEGRATOR_LINEAR:
        for (i = 0; i < len; ++i) {
          S = self->psd_int[i];
          c = self->psd_com[i];
          
          y = data[i] - c;
          t = S + y;

          self->psd_int[i] = t;
          self->psd_com[i]  = (t - S) - y;
        }
        break;

      case SUSCLI_VECTOR_INTEGRATOR_LOG:
        for (i = 0; i < len; ++i) {
          S = self->psd_int[i];
          c = self->psd_com[i];
          
          y = SU_LN(data[i]) - c;
          t = S + y;

          self->psd_int[i] = t;
          self->psd_com[i]  = (t - S) - y;
        }
        break;
      
      case SUSCLI_VECTOR_INTEGRATOR_MAX:
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
      case SUSCLI_VECTOR_INTEGRATOR_LINEAR:
        for (i = 0; i < len; ++i) {
          self->psd_int[i] *= self->Kinv;
          self->psd_com[i]  = 0;
        }
        break;

      case SUSCLI_VECTOR_INTEGRATOR_LOG:
        for (i = 0; i < len; ++i) {
          self->psd_int[i] = SU_EXP(self->Kinv * self->psd_int[i]);
          self->psd_com[i] = 0;
        }
        break;

      case SUSCLI_VECTOR_INTEGRATOR_MAX:
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
  suscan_source_config_t               *profile;
  enum sigutils_channel_detector_window window;
  SUFLOAT                               fft_rate;
  int32_t                               fft_size;
  int32_t                               fft_num;
  int32_t                               psd_per_dataset;
  enum suscli_vector_integrator_type    integrator;
  const char                           *prefix;
  SUBOOL                                overwrite;
  int32_t                               start;
  SUBOOL                                dc_cancel;
};

#define suscli_spectrum_params_INITIALIZER {   \
  NULL,  /* profile  */                        \
  SU_CHANNEL_DETECTOR_WINDOW_BLACKMANN_HARRIS, \
  25,    /* psd_rate */                        \
  8192,  /* fft_size */                        \
  10,    /* fft_num  */                        \
  10,    /* fft_per_dataset */                 \
  SUSCLI_VECTOR_INTEGRATOR_LINEAR,             \
  NULL,  /* prefix */                          \
  SU_FALSE, /* overwrite */                    \
  1,     /* start*/                            \
  SU_FALSE, /* dc_cancel */                    \
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
  SUFLOAT                       fft_rate;
  SUFREQ                        f0;

  /*
   * Analyzer state
   */
  SUBOOL                        have_source_info;
  SUBOOL                        have_analyzer_params;

  /* 
   * Output files of the current dataset
   */
  int32_t                       dataset;
  uint64_t                      count;
  char                         *odir;
  PTR_LIST(char,                file_path);

  FILE                         *ts_fp;
  npy_file_t                   *ts_npy;

  FILE                         *sdata_fp;
  npy_file_t                   *sdata_npy;
  SUBOOL                        warned;
  /*
   * Integration state
   */
  suscli_vector_integrator_t *integrator;
};

typedef struct suscli_spectrum suscli_spectrum_t;

SU_METHOD(suscli_spectrum, void, close_dataset)
{
  unsigned int i;

  /* Ensure NPY files are flushed before compressing */
  if (self->ts_npy != NULL) {
    SU_DISPOSE(npy_file, self->ts_npy);
    self->ts_npy = NULL;
  }

  if (self->sdata_npy != NULL) {
    SU_DISPOSE(npy_file, self->sdata_npy);
    self->sdata_npy = NULL;
  }

  if (self->odir != NULL && self->count > 0 && access(self->odir, F_OK) != -1) {
    char *cmd;
    int ret;

    if ((cmd = strbuild("zip -jr %s.npz %s > /dev/null", self->odir, self->odir)) != NULL) {
      ret = system(cmd);
      free(cmd);

      if (ret == 127) {
        if (!self->warned) {
          SU_WARNING("zip command is not available. Leaving datasets uncompressed\n");
          self->warned = SU_TRUE;
        }
      } else if (ret != 0) {
        SU_WARNING("zip command failed. Leaving current dataset uncompressed\n");
      } else {
        self->warned = SU_FALSE;

        for (i = 0; i < self->file_path_count; ++i)
          if (unlink(self->file_path_list[i]) == -1) {
            SU_WARNING("Cannot unlink %s: %s\n", self->file_path_list[i], strerror(errno));
            break;
          }

        if (i == self->file_path_count)
          if (rmdir(self->odir) == -1)
            SU_WARNING("Cannot remove directory %s: %s\n", self->odir, strerror(errno));
      }
    }
  }

  if (self->ts_fp != NULL) {
    fclose(self->ts_fp);
    self->ts_fp = NULL;
  }

  if (self->sdata_fp != NULL) {
    fclose(self->sdata_fp);
    self->sdata_fp = NULL;
  }

  for (i = 0; i < self->file_path_count; ++i)
    if (self->file_path_list[i] != NULL)
      free(self->file_path_list[i]);

  if (self->file_path_list != NULL)
    free(self->file_path_list);
  
  self->file_path_list  = NULL;
  self->file_path_count = 0;

  if (self->odir != NULL) {
    free(self->odir);
    self->odir = NULL;
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

SUINLINE
SU_METHOD(suscli_spectrum, SUBOOL, save_float, const char *name, SUFLOAT val)
{
  char *tmp = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRY(tmp = strbuild("%s/%s.npy", self->odir, name));
  SU_TRY(npy_file_store_float32(tmp, &val, 1));
  SU_TRYC(PTR_LIST_APPEND_CHECK(self->file_path, tmp));
  tmp = NULL;

  ok = SU_TRUE;

done:
  if (tmp != NULL)
    free(tmp);
  
  return ok;
}

SUINLINE
SU_METHOD(suscli_spectrum, SUBOOL, save_int32, const char *name, int32_t val)
{
  char *tmp = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRY(tmp = strbuild("%s/%s.npy", self->odir, name));
  SU_TRY(npy_file_store_int32(tmp, &val, 1));
  SU_TRYC(PTR_LIST_APPEND_CHECK(self->file_path, tmp));
  tmp = NULL;

  ok = SU_TRUE;

done:
  if (tmp != NULL)
    free(tmp);
  
  return ok;
}

SUINLINE
SU_METHOD(suscli_spectrum, SUBOOL, save_freq, const char *name, SUFREQ freq)
{
  char *tmp = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRY(tmp = strbuild("%s/%s.npy", self->odir, name));
  SU_TRY(npy_file_store_float64(tmp, &freq, 1));
  SU_TRYC(PTR_LIST_APPEND_CHECK(self->file_path, tmp));
  tmp = NULL;

  ok = SU_TRUE;

done:
  if (tmp != NULL)
    free(tmp);
  
  return ok;
}

SU_METHOD(suscli_spectrum, SUBOOL, open_dataset)
{
  SUBOOL ok = SU_FALSE;
  char *ts_path = NULL, *sdata_path = NULL;
  uint32_t dataset = ++self->dataset;

  SU_TRY(self->odir = strbuild("%s_%05d", self->prefix, dataset));
  
  if (access(self->odir, F_OK) != -1) {
    if (!self->params.overwrite) {
      SU_ERROR("Cannot create output directory %s: file exists\n", self->odir);
      goto done;
    }
  } else if (mkdir(self->odir, 0755) == -1) {
    SU_ERROR(
      "Cannot create output directory %s: %s\n",
      self->odir,
      strerror(errno));
    goto done;
  }

  SU_TRY(ts_path    = strbuild("%s/ts.npy",    self->odir));
  SU_TRY(sdata_path = strbuild("%s/sdata.npy", self->odir));

  SU_TRY(suscli_spectrum_save_float(self, "samp_rate", self->samp_rate));
  SU_TRY(suscli_spectrum_save_float(self, "fft_rate",  self->fft_rate));
  SU_TRY(suscli_spectrum_save_int32(self, "fft_num",   self->params.fft_num));
  SU_TRY(suscli_spectrum_save_freq(self,  "freq",      self->f0));

  if ((self->ts_fp = fopen(ts_path, "wb")) == NULL) {
    SU_ERROR("Cannot open %s for writing: %s\n", ts_path, strerror(errno));
    goto done;
  }
  SU_TRYC(PTR_LIST_APPEND_CHECK(self->file_path, ts_path));
  ts_path = NULL;

  SU_MAKE(self->ts_npy, npy_file, self->ts_fp, NPY_DTYPE_INT32, 1, 2, 0);
  if ((self->sdata_fp = fopen(sdata_path, "wb")) == NULL) {
    SU_ERROR("Cannot open %s for writing: %s\n", sdata_path, strerror(errno));
    goto done;
  }
  SU_TRYC(PTR_LIST_APPEND_CHECK(self->file_path, sdata_path));
  sdata_path = NULL;

  SU_MAKE(
    self->sdata_npy,
    npy_file,
    self->sdata_fp, NPY_DTYPE_FLOAT32, 1, self->params.fft_size, 0);
  
  ok = SU_TRUE;

done:
  if (ts_path != NULL)
    free(ts_path);

  if (sdata_path != NULL)
    free(sdata_path);

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
  new->fft_rate = 25;
  new->dataset  = params->start - 1;

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

  while(*new->prefix != '\0' && !isalnum(new->prefix[strlen(new->prefix) - 1]))
    new->prefix[strlen(new->prefix) - 1] = '\0';

  if (*new->prefix == '\0') {
    SU_ERROR("Invalid prefix name for dataset\n");
    goto fail;
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
      self->f0               = info->frequency;
      self->have_source_info = SU_TRUE;
      break;

    case SUSCAN_ANALYZER_MESSAGE_TYPE_PARAMS:
      params                     = msg->privdata;
      self->fft_rate             = 1. / params->psd_update_int;
      self->analyzer_params      = *params;
      self->have_analyzer_params = SU_TRUE;
      break;
  }

  if (self->have_source_info && self->have_analyzer_params) {
    self->analyzer_params.psd_update_int              = 1 / self->params.fft_rate;
    self->analyzer_params.detector_params.window_size = self->params.fft_size;
    self->analyzer_params.detector_params.window      = self->params.window;
    
    self->state = SUSCLI_SPECTRUM_CONFIGURING;

    SU_TRY(suscan_analyzer_set_dc_remove(self->analyzer, self->params.dc_cancel));

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

SUINLINE
SU_GETTER(suscli_spectrum, const char *, get_window_func)
{
  switch (self->params.window) {
    case SU_CHANNEL_DETECTOR_WINDOW_NONE:
      return "None";
    case SU_CHANNEL_DETECTOR_WINDOW_BLACKMANN_HARRIS:
      return "Blackmann-Harris";
    case SU_CHANNEL_DETECTOR_WINDOW_HAMMING:
      return "Hamming";
    case SU_CHANNEL_DETECTOR_WINDOW_HANN:
      return "Hann";
    case SU_CHANNEL_DETECTOR_WINDOW_FLAT_TOP:
      return "Flat-Top";
  }

  return "Unknown";
}

SUINLINE
SU_GETTER(suscli_spectrum, const char *, get_integrator)
{
  switch (self->params.integrator) {
    case SUSCLI_VECTOR_INTEGRATOR_LINEAR:
      return "Linear";
    case SUSCLI_VECTOR_INTEGRATOR_LOG:
      return "Logarithmic";
    case SUSCLI_VECTOR_INTEGRATOR_MAX:
      return "Maximum";
  }

  return "Unknown";
}

SUPRIVATE
SU_METHOD(suscli_spectrum, SUBOOL, process_configuring_message, const struct suscan_msg *msg)
{
  if (msg->type == SUSCAN_ANALYZER_MESSAGE_TYPE_PARAMS) {
    SUFLOAT psd_time, dataset_time;
    struct suscan_analyzer_params *params;
    char buf[16], buf2[16];

    params                     = msg->privdata;
    self->fft_rate             = 1. / params->psd_update_int;
    self->analyzer_params      = *params;

    if (params->detector_params.window_size != self->params.fft_size) {
      SU_ERROR("Analyzer rejected our FFT size. Refusing to continue\n");
      return SU_FALSE;
    }

    if (params->detector_params.window != self->params.window) {
      SU_ERROR("Analyzer rejected our FFT window function. Refusing to continue\n");
      return SU_FALSE;
    }

    if (!sufreleq(self->fft_rate, self->params.fft_rate, 1e-7)) {
      SU_ERROR("Analyzer rejected our PSD rate. Refusing to continue\n");
      return SU_FALSE;
    }

    psd_time     = self->params.fft_num / self->fft_rate;
    dataset_time = self->params.psd_per_dataset * psd_time;

    SU_INFO("Analyzer configured for spectrum acquisition\n");
    SU_INFO("  Center frequency:    %s\n", 
      suscan_units_format_frequency(self->f0, buf, 16));
    SU_INFO("  Sample rate:         %s\n", 
      suscan_units_format_frequency(self->samp_rate, buf, 16));
    SU_INFO("  DC cancel:           %s\n", self->params.dc_cancel ? "ON" : "OFF");
    SU_INFO("  Overwrite:           %s\n", self->params.overwrite ? "ON" : "OFF");
    SU_INFO("  Raw PSD rate:        %s\n", 
      suscan_units_format_frequency(self->fft_rate, buf, 16));
    SU_INFO("  Integrated PSD rate: %s (%s per PSD)\n", 
      suscan_units_format_frequency(1 / psd_time, buf, 16),
      suscan_units_format_time(psd_time, buf2, 16));
    SU_INFO("  Dataset span:        %s\n",
      suscan_units_format_time(dataset_time, buf, 16));

    SU_INFO("  Window function:     %s\n", suscli_spectrum_get_window_func(self));
    SU_INFO("  Integrator:          %s\n", suscli_spectrum_get_integrator(self));

    self->state = SUSCLI_SPECTRUM_ACQUIRING;
  }

  return SU_TRUE;
}

SUPRIVATE
SU_METHOD(suscli_spectrum, SUBOOL, process_acquiring_message, const struct suscan_msg *msg)
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
      
      if (++self->count == self->params.psd_per_dataset) {
        suscli_spectrum_close_dataset(self);
        SU_TRY(suscli_spectrum_open_dataset(self));
        new_dataset = SU_TRUE;
      }

      if (new_dataset)
        SU_INFO("Recording to dataset: %s\n", self->odir);
    }
  }

  ok = SU_TRUE;

done:
  return ok;
}

SUPRIVATE
SU_METHOD(suscli_spectrum, SUBOOL, process_message, const struct suscan_msg *msg)
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

SUPRIVATE SUBOOL
suscli_spectrum_params_parse(
  struct suscli_spectrum_params *sparm,
  const hashlist_t *params)
{
  SUBOOL ok = SU_FALSE;
  const char *tmp;

  SU_TRY(suscli_param_read_profile(params, "profile",  &sparm->profile));

  SU_TRY(suscli_param_read_float(params,  "fft-rate",  &sparm->fft_rate, sparm->fft_rate));
  SU_TRY(suscli_param_read_int(params,    "fft-size",  &sparm->fft_size, sparm->fft_size));
  SU_TRY(suscli_param_read_int(params,    "fft-num",   &sparm->fft_num,  sparm->fft_num));
  SU_TRY(suscli_param_read_int(params,    "ds-size",   &sparm->psd_per_dataset, sparm->psd_per_dataset));
  SU_TRY(suscli_param_read_int(params,    "ds-start",  &sparm->start, sparm->start));
  SU_TRY(suscli_param_read_bool(params,   "overwrite", &sparm->overwrite, sparm->overwrite));
  SU_TRY(suscli_param_read_bool(params,   "dc-cancel", &sparm->dc_cancel, sparm->dc_cancel));
  SU_TRY(suscli_param_read_string(params, "prefix",    &sparm->prefix, NULL));

  SU_TRY(suscli_param_read_string(params, "window",    &tmp, NULL));

  if (sparm->start < 0) {
    SU_ERROR("Invalid dataset start\n");
    goto done;
  }

  if (sparm->fft_rate <= 0) {
    SU_ERROR("Invalid FFT rate\n");
    goto done;
  }

  if (sparm->fft_size <= 0) {
    SU_ERROR("Invalid FFT size\n");
    goto done;
  }

  if (sparm->fft_num <= 0) {
    SU_ERROR("Invalid number of FFT integrations\n");
    goto done;
  }

  if (sparm->psd_per_dataset <= 0) {
    SU_ERROR("Invalid dataset size\n");
    goto done;
  }

  if (tmp != NULL) {
    if (strcasecmp(tmp, "none") == 0)
      sparm->window = SU_CHANNEL_DETECTOR_WINDOW_NONE;
    else if (strcasecmp(tmp, "hamming") == 0)
      sparm->window = SU_CHANNEL_DETECTOR_WINDOW_HAMMING;
    else if (strcasecmp(tmp, "hann") == 0)
      sparm->window = SU_CHANNEL_DETECTOR_WINDOW_HANN;
    else if (strcasecmp(tmp, "flat-top") == 0)
      sparm->window = SU_CHANNEL_DETECTOR_WINDOW_FLAT_TOP;
    else if (strcasecmp(tmp, "blackmann-harris") == 0)
      sparm->window = SU_CHANNEL_DETECTOR_WINDOW_BLACKMANN_HARRIS;
    else {
      SU_ERROR("Unsupported window function `%s'\n", tmp);
      SU_ERROR("Supported window functions are: none, hamming, hann, flat-top and blackmann-harris\n");
    }
  }

  SU_TRY(suscli_param_read_string(params, "integrator", &tmp, NULL));

  if (tmp != NULL) {
    if (strcasecmp(tmp, "linear") == 0)
      sparm->integrator = SUSCLI_VECTOR_INTEGRATOR_LINEAR;
    else if (strcasecmp(tmp, "log") == 0)
      sparm->integrator = SUSCLI_VECTOR_INTEGRATOR_LOG;
    else if (strcasecmp(tmp, "max") == 0)
      sparm->integrator = SUSCLI_VECTOR_INTEGRATOR_MAX;
    else {
      SU_ERROR("Unsupported spectrum integrator `%s'\n", tmp);
      SU_ERROR("Supported integrators are: linear, log and max\n");
    }
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
  
  SU_TRY(suscli_spectrum_params_parse(&sparm, params));

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

