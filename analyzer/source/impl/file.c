/*

  Copyright (C) 2023 Gonzalo José Carracedo Carballal

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

#include "file.h"
#include <analyzer/source.h>
#include <sys/time.h>

#ifdef _SU_SINGLE_PRECISION
#  define sf_read sf_read_float
#else
#  define sf_read sf_read_double
#endif


SUPRIVATE SNDFILE *
suscan_source_config_open_file_raw(
  const suscan_source_config_t *self,
  int sf_format,
  SF_INFO *sf_info)
{
  SNDFILE *sf = NULL;

  memset(sf_info, 0, sizeof(SF_INFO));

  sf_info->format = SF_FORMAT_RAW | sf_format | SF_ENDIAN_LITTLE;
  sf_info->channels = 2;
  sf_info->samplerate = 1000; /* libsndfile became a smartass with the years */

  if ((sf = sf_open(
      self->path,
      SFM_READ,
      sf_info)) == NULL) {
    SU_ERROR(
        "Failed to open %s as raw file: %s\n",
        self->path,
        sf_strerror(NULL));
  }
  
  /* Yeah, whatever. */
  sf_info->samplerate = self->samp_rate;

  return sf;
}

SUPRIVATE const char *
suscan_source_config_helper_sf_format_to_str(int format)
{
  SF_FORMAT_INFO info;
  int i, count;

  sf_command(NULL, SFC_GET_FORMAT_SUBTYPE_COUNT, &count, sizeof (int)) ;

  for (i = 0; i < count; ++i) {
    info.format = i;

    if (sf_command(NULL, SFC_GET_FORMAT_SUBTYPE, &info, sizeof(SF_FORMAT_INFO)) == 0)
      if (info.format == format)
        return info.name;
  }

  return "Unknown format";
}

SUPRIVATE SNDFILE *
suscan_source_config_sf_open(const suscan_source_config_t *self, SF_INFO *sf_info)
{
  SNDFILE *sf = NULL;
  const char *p;
  int guessed = -1;

 if (self->path == NULL) {
    SU_ERROR("Cannot open file source: path not set\n");
    return NULL;
  }

  /* Make sure we start on a known state */
  memset(sf_info, 0, sizeof(SF_INFO));

  switch (self->format) {
    case SUSCAN_SOURCE_FORMAT_WAV:
    case SUSCAN_SOURCE_FORMAT_AUTO:
      /* Autodetect: open as wav and, if failed, attempt to open as raw */
      sf_info->format = 0;
      if ((sf = sf_open(
          self->path,
          SFM_READ,
          sf_info)) != NULL) {
        SU_INFO(
            "WAV file source opened, sample rate = %d\n",
            sf_info->samplerate);
        break;
      } else if (self->format == SUSCAN_SOURCE_FORMAT_WAV) {
        SU_ERROR(
            "Failed to open %s as audio file: %s\n",
            self->path,
            sf_strerror(NULL));
        return NULL;
      } else {
        /* Guess by extension */
        if ((p = strrchr(self->path, '.')) != NULL) {
          ++p;
          if (strcasecmp(p, "cu8") == 0 || strcasecmp(p, "u8") == 0)
            guessed = SF_FORMAT_PCM_U8;
          else if (strcasecmp(p, "cs16") == 0 || strcasecmp(p, "s16") == 0)
            guessed = SF_FORMAT_PCM_16;
          else if (strcasecmp(p, "cf32") == 0 || strcasecmp(p, "raw") == 0)
            guessed = SF_FORMAT_FLOAT;
        }

        if (guessed == -1) {
          guessed = SUSCAN_SOURCE_FORMAT_FALLBACK;
          SU_INFO(
            "Unrecognized file extension (%s), assuming %s\n", 
            p,
            suscan_source_config_helper_sf_format_to_str(guessed));
        } else {
          SU_INFO(
            "Data format detected: %s\n",
            suscan_source_config_helper_sf_format_to_str(guessed));
        }

        sf = suscan_source_config_open_file_raw(self, guessed, sf_info);
      }
      break;

    case SUSCAN_SOURCE_FORMAT_RAW_FLOAT32:
      sf = suscan_source_config_open_file_raw(self, SF_FORMAT_FLOAT, sf_info);
      break;

    case SUSCAN_SOURCE_FORMAT_RAW_UNSIGNED8:
      sf = suscan_source_config_open_file_raw(self, SF_FORMAT_PCM_U8, sf_info);
      break;

    case SUSCAN_SOURCE_FORMAT_RAW_SIGNED16:
      sf = suscan_source_config_open_file_raw(self, SF_FORMAT_PCM_16, sf_info);
      break;
    
    case SUSCAN_SOURCE_FORMAT_RAW_SIGNED8:
      sf = suscan_source_config_open_file_raw(self, SF_FORMAT_PCM_S8, sf_info);
      break;
  }

  return sf;
}

SUBOOL
suscan_source_config_file_is_valid(const suscan_source_config_t *self)
{
  SUBOOL ok = SU_FALSE;
  SNDFILE *sf = NULL;
  SF_INFO sf_info;

  if ((sf = suscan_source_config_sf_open(self, &sf_info)) != NULL) {
    sf_close(sf);
    ok = SU_TRUE;
  }

  return ok;
}

/****************************** Implementation ********************************/
SUPRIVATE void
suscan_source_file_close(void *ptr)
{
  struct suscan_source_file *self = (struct suscan_source_file *) ptr;

  if (self->sf != NULL)
    sf_close(self->sf);
  
  free(self);
}

SUPRIVATE SUBOOL
suscan_source_config_file_check(const suscan_source_config_t *config)
{
  if (config->samp_rate < 1
      && !(config->type == SUSCAN_SOURCE_TYPE_FILE
          && config->format == SUSCAN_SOURCE_FORMAT_WAV)) {
    SU_ERROR("Sample rate cannot be zero!\n");
    return SU_FALSE;
  }

  return SU_TRUE;
}


SUPRIVATE void *
suscan_source_file_open(
  suscan_source_t *source,
  suscan_source_config_t *config,
  struct suscan_source_info *info)
{
  struct suscan_source_file *new = NULL;
  struct timeval elapsed;

  SU_TRY_FAIL(suscan_source_config_file_check(config));
  SU_ALLOCATE_FAIL(new, struct suscan_source_file);

  new->config = config;
  new->sf     = suscan_source_config_sf_open(config, &new->sf_info);
  
  if (new->sf == NULL)
    goto fail;

  new->iq_file   = new->sf_info.channels == 2;

  /* Initialize source info */
  suscan_source_info_init(info);
  info->permissions         = SUSCAN_ANALYZER_ALL_FILE_PERMISSIONS;
  info->permissions        &= ~SUSCAN_ANALYZER_PERM_SET_DC_REMOVE;

  info->source_samp_rate    = new->sf_info.samplerate;
  info->effective_samp_rate = new->sf_info.samplerate;
  info->measured_samp_rate  = new->sf_info.samplerate;
  info->source_start        = config->start_time;
  
  elapsed.tv_sec            = new->sf_info.frames / info->source_samp_rate;
  elapsed.tv_usec           = 
    (1000000 
      * (new->sf_info.frames - elapsed.tv_sec * info->source_samp_rate))
      / info->source_samp_rate;

  timeradd(&info->source_start, &elapsed, &info->source_end);

  new->samp_rate = (SUFLOAT) info->source_samp_rate;

  return new;

fail:
  if (new != NULL)
    suscan_source_file_close(new);

  return NULL;
}

SUPRIVATE SUBOOL
suscan_source_file_start(void *self)
{
  return SU_TRUE;
}

SUPRIVATE SUSDIFF
suscan_source_file_read(
  void *userdata,
  SUCOMPLEX *buf,
  SUSCOUNT max)
{
  struct suscan_source_file *self = (struct suscan_source_file *) userdata;
  SUFLOAT *as_real;
  int got, i;
  unsigned int real_count;

  if (self->force_eos)
    return 0;

  if (max > SUSCAN_SOURCE_DEFAULT_BUFSIZ)
    max = SUSCAN_SOURCE_DEFAULT_BUFSIZ;

  real_count = max * (self->iq_file ? 2 : 1);

  as_real = (SUFLOAT *) buf;

  got = sf_read(self->sf, as_real, real_count);

  if (got == 0 && self->config->loop) {
    if (sf_seek(self->sf, 0, SEEK_SET) == -1) {
      SU_ERROR("Failed to seek to the beginning of the stream\n");
      return 0;
    }
    self->looped = SU_TRUE;
    self->total_samples = 0;
    got = sf_read(self->sf, as_real, real_count);
  }

  if (got > 0) {
    /* Real data mode: iteratively cast to complex */
    if (self->sf_info.channels == 1) {
      for (i = got - 1; i >= 0; --i)
        buf[i] = as_real[i];
    } else {
      got >>= 1;
    }

    self->total_samples += got;
  }

  return got;
}

SUPRIVATE void
suscan_source_file_get_time(void *userdata, struct timeval *tv)
{
  struct suscan_source_file *self = (struct suscan_source_file *) userdata;
  struct timeval elapsed;
  SUSCOUNT samp_count = self->total_samples;
  SUFLOAT samp_rate = self->samp_rate;

  elapsed.tv_sec  = samp_count / samp_rate;
  elapsed.tv_usec = 
    (1000000 
      * (samp_count - elapsed.tv_sec * samp_rate))
      / samp_rate;

  timeradd(&self->config->start_time, &elapsed, tv);
}

SUPRIVATE SUBOOL
suscan_source_file_seek(void *userdata, SUSCOUNT pos)
{
  struct suscan_source_file *self = (struct suscan_source_file *) userdata;

  if (sf_seek(self->sf, pos, SEEK_SET) == -1)
    return SU_FALSE;

  self->total_samples = pos;

  return SU_TRUE;
}

SUPRIVATE SUSDIFF
suscan_source_file_max_size(void *userdata)
{
  struct suscan_source_file *self = (struct suscan_source_file *) userdata;

  return self->sf_info.frames;
}

SUPRIVATE SUBOOL
suscan_source_file_cancel(void *userdata)
{
  struct suscan_source_file *self = (struct suscan_source_file *) userdata;

  self->force_eos = SU_TRUE;

  return SU_TRUE;
}

SUPRIVATE SUSDIFF
suscan_source_file_estimate_size(const suscan_source_config_t *config)
{
  SNDFILE *sf = NULL;
  SF_INFO sf_info;
  SUSDIFF max_size = -1;

  sf = suscan_source_config_sf_open(config, &sf_info);
  if (sf == NULL)
    goto done;
    
  max_size = sf_info.frames - 1;

done:
  if (sf != NULL)
    sf_close(sf);
  
  return max_size;
}

SUPRIVATE struct suscan_source_interface g_file_source =
{
  .name            = "file",
  .desc            = NULL,
  .realtime        = SU_FALSE,

  .open            = suscan_source_file_open,
  .close           = suscan_source_file_close,
  .estimate_size   = suscan_source_file_estimate_size,
  .start           = suscan_source_file_start,
  .cancel          = suscan_source_file_cancel,
  .read            = suscan_source_file_read,
  .seek            = suscan_source_file_seek,
  .max_size        = suscan_source_file_max_size,
  .get_time        = suscan_source_file_get_time,

  /* Unset members */
  .set_frequency   = NULL,
  .set_gain        = NULL,
  .set_antenna     = NULL,
  .set_bandwidth   = NULL,
  .set_ppm         = NULL,
  .set_dc_remove   = NULL,
  .set_agc         = NULL,
  .get_freq_limits = NULL,
};

SUBOOL
suscan_source_register_file(void)
{
  int ndx;
  SUBOOL ok = SU_FALSE;
  static char *desc;

  SU_TRY(desc = strbuild("Capture file (%s)", sf_version_string()));

  g_file_source.desc = desc;
  
  SU_TRYC(ndx = suscan_source_register(&g_file_source));
  SU_TRYC(ndx == SUSCAN_SOURCE_TYPE_FILE);

  ok = SU_TRUE;

done:
  return ok;
}
