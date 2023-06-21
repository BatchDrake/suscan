/*

  Copyright (C) 2017 Gonzalo José Carracedo Carballal

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

#ifndef _SOURCE_H
#define _SOURCE_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <sndfile.h>
#include <string.h>
#include <sigutils/sigutils.h>
#include <SoapySDR/Device.h>
#include <SoapySDR/Formats.h>
#include <SoapySDR/Version.h>
#include <analyzer/serialize.h>
#include <analyzer/source/device.h>
#include <analyzer/source/config.h>
#include <sgdp4/sgdp4-types.h>
#include <sigutils/util/util.h>

#if defined(_WIN32) && defined(interface)
#  undef interface
#endif /* interface */

#define SUSCAN_SOURCE_DEFAULT_BUFSIZ 1024

#define SUSCAN_SOURCE_SETTING_PREFIX    "setting:"
#define SUSCAN_SOURCE_SETTING_PFXLEN    (sizeof("setting:") - 1)

#define SUSCAN_SOURCE_DEFAULT_READ_TIMEOUT 100000 /* 100 ms */
#define SUSCAN_SOURCE_ANTIALIAS_REL_SIZE    5
#define SUSCAN_SOURCE_DECIMATOR_BUFFER_SIZE 512

#define SUSCAN_SOURCE_DC_AVERAGING_PERIOD   10
#define SUSCAN_SOURCE_DECIM_INNER_GUARD     5e-2

/* Permissions */
#define SUSCAN_ANALYZER_PERM_HALT               (1ull << 0)
#define SUSCAN_ANALYZER_PERM_SET_FREQ           (1ull << 1)
#define SUSCAN_ANALYZER_PERM_SET_GAIN           (1ull << 2)
#define SUSCAN_ANALYZER_PERM_SET_ANTENNA        (1ull << 3)
#define SUSCAN_ANALYZER_PERM_SET_BW             (1ull << 4)
#define SUSCAN_ANALYZER_PERM_SET_PPM            (1ull << 5)
#define SUSCAN_ANALYZER_PERM_SET_DC_REMOVE      (1ull << 6)
#define SUSCAN_ANALYZER_PERM_SET_IQ_REVERSE     (1ull << 7)
#define SUSCAN_ANALYZER_PERM_SET_AGC            (1ull << 8)
#define SUSCAN_ANALYZER_PERM_OPEN_AUDIO         (1ull << 9)
#define SUSCAN_ANALYZER_PERM_OPEN_RAW           (1ull << 10)
#define SUSCAN_ANALYZER_PERM_OPEN_INSPECTOR     (1ull << 11)
#define SUSCAN_ANALYZER_PERM_SET_FFT_SIZE       (1ull << 12)
#define SUSCAN_ANALYZER_PERM_SET_FFT_FPS        (1ull << 13)
#define SUSCAN_ANALYZER_PERM_SET_FFT_WINDOW     (1ull << 14)
#define SUSCAN_ANALYZER_PERM_SEEK               (1ull << 15)
#define SUSCAN_ANALYZER_PERM_THROTTLE           (1ull << 16)
#define SUSCAN_ANALYZER_PERM_SET_BB_FILTER      (1ull << 17)

#define SUSCAN_ANALYZER_PERM_ALL              0xffffffffffffffffull

#define SUSCAN_ANALYZER_ALL_FILE_PERMISSIONS \
  (SUSCAN_ANALYZER_PERM_ALL &                \
  ~(SUSCAN_ANALYZER_PERM_SET_GAIN       |    \
    SUSCAN_ANALYZER_PERM_SET_ANTENNA    |    \
    SUSCAN_ANALYZER_PERM_SET_BW         |    \
    SUSCAN_ANALYZER_PERM_SET_PPM        |    \
    SUSCAN_ANALYZER_PERM_SET_AGC))

#define SUSCAN_ANALYZER_ALL_SDR_PERMISSIONS  \
  (SUSCAN_ANALYZER_PERM_ALL &                \
  ~(SUSCAN_ANALYZER_PERM_SEEK |              \
    SUSCAN_ANALYZER_PERM_THROTTLE))

struct sigutils_specttuner;
struct sigutils_specttuner_channel;

/****************************** Source API ***********************************/
SUSCAN_SERIALIZABLE(suscan_source_gain_info) {
  char *name;
  SUFLOAT min;
  SUFLOAT max;
  SUFLOAT step;
  SUFLOAT value;
};

/*!
 * Constructor for gain info objects.
 * \param value gain value object describing this gain element
 * \return a pointer to the created object or NULL on failure
 * \author Gonzalo José Carracedo Carballal
 */
struct suscan_source_gain_info *suscan_source_gain_info_new(
    const struct suscan_source_gain_value *value);

/*!
 * Constructor for gain info objects (value only).
 * \param name name of the gain element
 * \param value value of this gain in dBs
 * \return a pointer to the created object or NULL on failure
 * \author Gonzalo José Carracedo Carballal
 */
struct suscan_source_gain_info *
suscan_source_gain_info_new_value_only(
    const char *name,
    SUFLOAT value);

/*!
 * Copy-constructor for gain info objects.
 * \param old existing gain info object
 * \return a pointer to the created object or NULL on failure
 * \author Gonzalo José Carracedo Carballal
 */
struct suscan_source_gain_info *
suscan_source_gain_info_dup(
    const struct suscan_source_gain_info *old);

/*!
 * Destructor of the gain info object.
 * \param self pointer to the gain info object
 * \author Gonzalo José Carracedo Carballal
 */
void suscan_source_gain_info_destroy(struct suscan_source_gain_info *self);

SUSCAN_SERIALIZABLE(suscan_source_info) {
  uint64_t permissions;

  uint32_t mtu;

  SUSCOUNT source_samp_rate;
  SUSCOUNT effective_samp_rate;
  SUFLOAT  measured_samp_rate;

  SUFREQ   frequency;
  SUFREQ   freq_min;
  SUFREQ   freq_max;
  SUFREQ   lnb;

  SUFLOAT  bandwidth;
  SUFLOAT  ppm;
  char    *antenna;
  SUBOOL   dc_remove;
  SUBOOL   iq_reverse;
  SUBOOL   agc;
 
  SUBOOL   have_qth;
  xyz_t    qth;

  struct timeval source_time;

  SUBOOL         seekable;
  struct timeval source_start;
  struct timeval source_end;

  PTR_LIST(struct suscan_source_gain_info, gain);
  PTR_LIST(char, antenna);
};

/*!
 * Initialize a source information structure
 * \param self a pointer to the source info structure
 * \author Gonzalo José Carracedo Carballal
 */
void suscan_source_info_init(struct suscan_source_info *self);

/*!
 * Initialize a source information structure from an existing
 * source information
 * \param self a pointer to the source info structure to be initialized
 * \param origin a pointer to the source info structure to copy
 * \return SU_TRUE on success, SU_FALSE on failure
 * \author Gonzalo José Carracedo Carballal
 */
SUBOOL suscan_source_info_init_copy(
    struct suscan_source_info *self,
    const struct suscan_source_info *origin);

/*!
 * Release allocated resources in the source information structure
 * \param self a pointer to the source info structure
 * \author Gonzalo José Carracedo Carballal
 */
void suscan_source_info_finalize(struct suscan_source_info *self);

/************** Source interface: to be implemented by all sources ************/
struct suscan_source;
struct suscan_source_interface {
  const char *name;
  
  void   *(*open) (
    struct suscan_source *source,
    suscan_source_config_t *config,
    struct suscan_source_info *info);
  void    (*close) (void *);

  SUBOOL  (*start) (void *);
  SUBOOL  (*cancel) (void *);

  SUSDIFF (*read) (void *, SUCOMPLEX *buffer, SUSCOUNT max);
  SUSDIFF (*max_size) (void *);
  
  void    (*get_time) (void *, struct timeval *tv);
  SUBOOL  (*seek) (void *,  SUSCOUNT samples);

  SUBOOL   (*set_frequency) (void *, SUFREQ freq);
  SUBOOL   (*set_gain) (void *, const char *name, SUFLOAT value);
  SUBOOL   (*set_antenna) (void *, const char *);
  SUBOOL   (*set_bandwidth) (void *, SUFLOAT);
  SUBOOL   (*set_ppm) (void *, SUFLOAT);
  SUBOOL   (*set_dc_remove) (void *, SUBOOL);
  SUBOOL   (*set_agc) (void *, SUBOOL);
  unsigned (*get_samp_rate) (void *);
};

struct suscan_source {
  suscan_source_config_t *config; /* Source may alter configuration! */
  const struct suscan_source_interface *iface;
  struct suscan_source_info info;

  SUBOOL   capturing;
  void    *source; /* Opaque source object */

  SUBOOL   looped;
  SUBOOL   soft_dc_correction;
  SUBOOL   soft_iq_balance;
  SUSCOUNT total_samples;
  
  SUBOOL    use_soft_dc;
  SUSCOUNT  soft_dc_train_samples;
  SUSCOUNT  soft_dc_count;
  SUBOOL    have_dc_offset;
  SUFLOAT   soft_dc_alpha;
  SUCOMPLEX dc_offset;
  SUCOMPLEX dc_c;

  /* To prevent source from looping forever */
  SUBOOL force_eos;

  /* Downsampling members */
  struct sigutils_specttuner         *decimator;
  struct sigutils_specttuner_channel *main_channel;
  SUCOMPLEX *read_buf;
  SUCOMPLEX *curr_buf;
  SUSCOUNT   curr_size;
  SUSCOUNT   curr_ptr;

  SUCOMPLEX *decim_spillover;
  SUSCOUNT   decim_spillover_alloc;
  SUSCOUNT   decim_spillover_size;
  SUSCOUNT   decim_spillover_ptr;

  int decim;
};

typedef struct suscan_source suscan_source_t;

/* Construction and destruction */
suscan_source_t *suscan_source_new(suscan_source_config_t *config);
void suscan_source_destroy(suscan_source_t *config);

/* Interface methods */
SUBOOL suscan_source_start_capture(suscan_source_t *source);
SUBOOL suscan_source_stop_capture(suscan_source_t *source);

SUSDIFF suscan_source_read(
    suscan_source_t *source,
    SUCOMPLEX *buffer,
    SUSCOUNT max);
SUSDIFF  suscan_source_get_max_size(const suscan_source_t *self);

void   suscan_source_get_time(suscan_source_t *self, struct timeval *tv);
SUBOOL suscan_source_seek(suscan_source_t *self, SUSCOUNT);

SUFREQ suscan_source_get_freq(const suscan_source_t *source);
SUBOOL suscan_source_set_freq(suscan_source_t *source, SUFREQ freq);
SUBOOL suscan_source_set_lnb_freq(suscan_source_t *source, SUFREQ freq);
SUBOOL suscan_source_set_freq2(suscan_source_t *source, SUFREQ freq, SUFREQ lnb);
SUBOOL suscan_source_set_gain(
    suscan_source_t *source,
    const char *name,
    SUFLOAT gain);
SUBOOL suscan_source_set_antenna(suscan_source_t *source, const char *name);
SUBOOL suscan_source_set_bandwidth(suscan_source_t *source, SUFLOAT bw);
SUBOOL suscan_source_set_ppm(suscan_source_t *source, SUFLOAT ppm);
SUBOOL suscan_source_set_dc_remove(suscan_source_t *source, SUBOOL remove);
SUBOOL suscan_source_set_agc(suscan_source_t *source, SUBOOL set);

/* Other API methods */
SUSCOUNT suscan_source_get_dc_samples(const suscan_source_t *self);
SUSCOUNT suscan_source_get_consumed_samples(const suscan_source_t *self);
SUSCOUNT suscan_source_get_base_samp_rate(const suscan_source_t *self);
void     suscan_source_get_end_time(
  const suscan_source_t *self, 
  struct timeval *tv);

SUINLINE void 
suscan_source_get_start_time(
  const suscan_source_t *self,
  struct timeval *tv)
{
  suscan_source_config_get_start_time(self->config, tv);
}

SUINLINE void 
suscan_source_set_start_time(
  suscan_source_t *self,
  struct timeval tv)
{
  suscan_source_config_set_start_time(self->config, tv);
}

SUINLINE SUBOOL
suscan_source_has_looped(suscan_source_t *self)
{
  SUBOOL looped = self->looped;
  self->looped = SU_FALSE;

  return looped;
}

SUINLINE enum suscan_source_type
suscan_source_get_type(const suscan_source_t *src)
{
  return src->config->type;
}

SUINLINE SUFLOAT
suscan_source_get_samp_rate(const suscan_source_t *src)
{
  if (src->capturing)
    return (SUFLOAT) src->info.source_samp_rate / src->decim;
  else
    return (SUFLOAT) src->config->samp_rate / src->config->average;
}

SUINLINE int
suscan_source_get_decimation(const suscan_source_t *self)
{
  return self->decim;
}

SUINLINE void
suscan_source_force_eos(suscan_source_t *src)
{
  src->force_eos = SU_TRUE;
}

SUINLINE const struct suscan_source_info *
suscan_source_get_info(const suscan_source_t *self)
{
  return &self->info;
}

SUINLINE const suscan_source_config_t *
suscan_source_get_config(const suscan_source_t *src)
{
  return src->config;
}

SUINLINE SUBOOL
suscan_source_is_capturing(const suscan_source_t *src)
{
  return src->capturing;
}


/******************************* Source interface *****************************/
SUBOOL suscan_source_config_register(suscan_source_config_t *config);

SUBOOL suscan_source_register(int, const struct suscan_source_interface *iface);
const struct suscan_source_interface *suscan_source_interface_lookup_by_index(int);
const struct suscan_source_interface *suscan_source_interface_lookup_by_name(const char *);

SUBOOL suscan_source_detect_devices(void);

/* Internal */
SUBOOL suscan_source_register_file(void);
SUBOOL suscan_source_register_soapysdr(void);

SUBOOL suscan_source_init_source_types(void);
SUBOOL suscan_init_sources(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _SOURCE_H */
