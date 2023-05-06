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

#ifndef _ANALYZER_H
#define _ANALYZER_H

#include <sigutils/sigutils.h>
#include <sigutils/detect.h>
#include <sigutils/smoothpsd.h>
#include <pthread.h>

#define _COMPAT_BARRIERS
#include <compat.h>

#include "worker.h"
#include "source.h"
#include "throttle.h"
#include "inspector/inspector.h"
#include "inspsched.h"
#include "serialize.h"

#include <sgdp4/sgdp4-types.h>

#include "mq.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define SUSCAN_ANALYZER_GUARD_BAND_PROPORTION 1.1
#define SUSCAN_ANALYZER_FS_MEASURE_INTERVAL   1.0

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

/* Default priorities */
#define SUSCAN_ANALYZER_BBFILT_PRIO_DEFAULT   0x7fffffffffffffffll

/* Entirely empirical */
#define SUSCAN_ANALYZER_SLOW_RATE             44100
#define SUSCAN_ANALYZER_SLOW_READ_SIZE        32
#define SUSCAN_ANALYZER_FAST_READ_SIZE        1024
#define SUSCAN_ANALYZER_MIN_POST_HOP_FFTS     7

struct suscan_analyzer;

/*!
 * \brief Analyzer object mode.
 *
 * Describes the behavior of the underlying source object: whether it will
 * be used to perform sweeps over a range of frequencies, or whether it will
 * be used to channelize its input.
 * \author Gonzalo José Carracedo Carballal
 */
enum suscan_analyzer_mode {
  SUSCAN_ANALYZER_MODE_CHANNEL,
  SUSCAN_ANALYZER_MODE_WIDE_SPECTRUM
};

/*!
 * \brief Analyzer parameters
 *
 * Set of Analyzer parameters passed to the constructor.
 * \author Gonzalo José Carracedo Carballal
 */
SUSCAN_SERIALIZABLE(suscan_analyzer_params) {
  enum suscan_analyzer_mode mode; /*!< Analyzer mode */
  struct sigutils_channel_detector_params detector_params; /*!< Channel detector parameters */
  SUFLOAT  channel_update_int; /*!< Channel info update interval (seconds) */
  SUFLOAT  psd_update_int;     /*!< Spectrum update interval (seconds) */
  SUFREQ   min_freq; /*!< Minimum sweep frequency (only in wide spectrum mode) */
  SUFREQ   max_freq; /*!< Maximum sweep frequency (only in wide spectrum mode) */
};

#define suscan_analyzer_params_INITIALIZER {                               \
  SUSCAN_ANALYZER_MODE_CHANNEL,                 /* mode */                  \
  sigutils_channel_detector_params_INITIALIZER, /* detector_params */       \
  SU_ADDSFX(0.1),                               /* channel_update_int */    \
  SU_ADDSFX(0.04),                              /* psd_update_int */        \
  0,                                            /* min_freq */              \
  0,                                            /* max_freq */              \
}

SUSCAN_SERIALIZABLE(suscan_analyzer_gain_info) {
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
struct suscan_analyzer_gain_info *suscan_analyzer_gain_info_new(
    const struct suscan_source_gain_value *value);

/*!
 * Constructor for gain info objects (value only).
 * \param name name of the gain element
 * \param value value of this gain in dBs
 * \return a pointer to the created object or NULL on failure
 * \author Gonzalo José Carracedo Carballal
 */
struct suscan_analyzer_gain_info *
suscan_analyzer_gain_info_new_value_only(
    const char *name,
    SUFLOAT value);

/*!
 * Copy-constructor for gain info objects.
 * \param old existing gain info object
 * \return a pointer to the created object or NULL on failure
 * \author Gonzalo José Carracedo Carballal
 */
struct suscan_analyzer_gain_info *
suscan_analyzer_gain_info_dup(
    const struct suscan_analyzer_gain_info *old);

/*!
 * Destructor of the gain info object.
 * \param self pointer to the gain info object
 * \author Gonzalo José Carracedo Carballal
 */
void suscan_analyzer_gain_info_destroy(struct suscan_analyzer_gain_info *self);

/*!
 * \brief Wideband analyzer sweep strategy
 *
 * Describes the frequency hopping strategy used by a wideband analyzer.
 * \author Gonzalo José Carracedo Carballal
 */
enum suscan_analyzer_sweep_strategy {
  SUSCAN_ANALYZER_SWEEP_STRATEGY_STOCHASTIC,
  SUSCAN_ANALYZER_SWEEP_STRATEGY_PROGRESSIVE,
};

/*!
 * \brief Wideband analyzer spectrum partitioning
 *
 * Describes the kind of frequency hops performed by the wideband analyzer
 * when using the random walk strategy, either in fixed-width hops (discrete)
 * or absolutely random (continuous)
 *
 * \author Gonzalo José Carracedo Carballal
 */
enum suscan_analyzer_spectrum_partitioning {
  SUSCAN_ANALYZER_SPECTRUM_PARTITIONING_DISCRETE,
  SUSCAN_ANALYZER_SPECTRUM_PARTITIONING_CONTINUOUS
};

/*!
 * \brief Wideband analyzer parameters
 *
 * Wideband analyzer parameters passed to the constructor
 *
 * \author Gonzalo José Carracedo Carballal
 */
struct suscan_analyzer_sweep_params {
  enum suscan_analyzer_sweep_strategy strategy;
  enum suscan_analyzer_spectrum_partitioning partitioning;

  SUFREQ min_freq;
  SUFREQ max_freq;
  SUSCOUNT fft_min_samples; /* Minimum number of FFT frames before updating */
};

/*!
 * \brief Function pointer to baseband filter
 *
 * Convenience typedef of the prototype of baseband filter functions
 * \author Gonzalo José Carracedo Carballal
 */
typedef SUBOOL (*suscan_analyzer_baseband_filter_func_t) (
      void *privdata,
      struct suscan_analyzer *analyzer,
      SUCOMPLEX *samples,
      SUSCOUNT length,
      SUSCOUNT consumed);

/*!
 * \brief Baseband filter description
 *
 * Structure holding a pointer to a function that would perform some kind
 * of baseband processing (i.e. before channelization).
 * \author Gonzalo José Carracedo Carballal
 */
struct suscan_analyzer_baseband_filter {
  suscan_analyzer_baseband_filter_func_t func;
  void *privdata;
};

SUSCAN_SERIALIZABLE(suscan_analyzer_source_info) {
  uint64_t permissions;
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

  PTR_LIST(struct suscan_analyzer_gain_info, gain);
  PTR_LIST(char, antenna);
};

/*!
 * Initialize a source information structure
 * \param self a pointer to the source info structure
 * \author Gonzalo José Carracedo Carballal
 */
void suscan_analyzer_source_info_init(
    struct suscan_analyzer_source_info *self);

/*!
 * Initialize a source information structure from an existing
 * source information
 * \param self a pointer to the source info structure to be initialized
 * \param origin a pointer to the source info structure to copy
 * \return SU_TRUE on success, SU_FALSE on failure
 * \author Gonzalo José Carracedo Carballal
 */
SUBOOL suscan_analyzer_source_info_init_copy(
    struct suscan_analyzer_source_info *self,
    const struct suscan_analyzer_source_info *origin);

/*!
 * Release allocated resources in the source information structure
 * \param self a pointer to the source info structure
 * \author Gonzalo José Carracedo Carballal
 */
void suscan_analyzer_source_info_finalize(
    struct suscan_analyzer_source_info *self);

struct suscan_analyzer_interface {
  const char *name;
  void  *(*ctor) (struct suscan_analyzer *, va_list);
  void   (*dtor) (void *);

  /* Source-related methods */
  SUBOOL   (*set_frequency) (void *, SUFREQ freq, SUFREQ lnb);
  SUBOOL   (*set_gain) (void *, const char *name, SUFLOAT value);
  SUBOOL   (*set_antenna) (void *, const char *);
  SUBOOL   (*set_bandwidth) (void *, SUFLOAT);
  SUBOOL   (*set_ppm) (void *, SUFLOAT);
  SUBOOL   (*set_dc_remove) (void *, SUBOOL);
  SUBOOL   (*set_iq_reverse) (void *, SUBOOL);
  SUBOOL   (*set_agc) (void *, SUBOOL);
  SUBOOL   (*force_eos) (void *);
  SUBOOL   (*is_real_time) (const void *);
  unsigned (*get_samp_rate) (const void *);
  SUFLOAT  (*get_measured_samp_rate) (const void *);
  void     (*get_source_time) (const void *, struct timeval *tv);
  SUBOOL   (*seek) (void *, const struct timeval *tv);
  SUBOOL   (*register_baseband_filter) (
    void *,
    suscan_analyzer_baseband_filter_func_t func,
    void *privdata,
    int64_t priority);
  
  struct suscan_analyzer_source_info *(*get_source_info_pointer) (const void *);
  SUBOOL   (*commit_source_info) (void *);

  /* Worker-specific methods */
  SUBOOL   (*set_sweep_strategy) (void *, enum suscan_analyzer_sweep_strategy);
  SUBOOL   (*set_spectrum_partitioning) (void *, enum suscan_analyzer_spectrum_partitioning);
  SUBOOL   (*set_hop_range) (void *, SUFREQ, SUFREQ);
  SUBOOL   (*set_buffering_size) (void *, SUSCOUNT);

  /* Fast methods */
  SUBOOL   (*set_inspector_frequency) (void *, SUHANDLE, SUFREQ);
  SUBOOL   (*set_inspector_bandwidth) (void *, SUHANDLE, SUFLOAT);

  /* Mesage passing */
  SUBOOL   (*write) (void *, uint32_t, void *);

  /* Loop management */
  void     (*req_halt) (void *);
};

struct suscan_analyzer {
  struct suscan_analyzer_params params;
  struct suscan_mq *mq_out; /* From-thread messages */
  const struct suscan_analyzer_interface *iface;
  void  *impl;

  SUBOOL have_impl_rt;
  struct timeval impl_rt_delta;
  
  SUBOOL running;
  SUBOOL halt_requested;
  SUBOOL eos;
};

/*!
 * \brief Analyzer class
 *
 * Main class as a front-end for all signal analysis task
 *
 * \author Gonzalo José Carracedo Carballal
 */
typedef struct suscan_analyzer suscan_analyzer_t;


/*!
 * Constructor for the analyzer object.
 * \param params pointer to the analyzer parameters.
 * \param config pointer to the signal source to act upon.
 * \param mq pointer to the user-provided message queue that the analyzer
 * object will use to store output messages.
 * \see suscan_analyzer_consume_mq
 * \see suscan_analyzer_dispose_message
 * \return SU_TRUE for success or SU_FALSE on failure
 * \author Gonzalo José Carracedo Carballal
 */
suscan_analyzer_t *suscan_analyzer_new(
    const struct suscan_analyzer_params *params,
    suscan_source_config_t *config,
    struct suscan_mq *mq);

/*!
 * Destroys the analyzer object, releasing its allocated resources.
 * \author Gonzalo José Carracedo Carballal
 */
void suscan_analyzer_destroy(suscan_analyzer_t *analyzer);

/*!
 * Returns whether the analyzer performs local processing
 * \param self a pointer to the analyzer object
 * \return SU_TRUE if the analyzer runs in the local machine, SU_FALSE otherwise
 * \author Gonzalo José Carracedo Carballal
 */
SUBOOL suscan_analyzer_is_local(const suscan_analyzer_t *self);

/******************************* Inlined methods ******************************/
/*!
 * Is the analyzer running on top of a real-time source?
 * \param analyzer a pointer to the analyzer object
 * \author Gonzalo José Carracedo Carballal
 */
SUINLINE SUBOOL
suscan_analyzer_is_real_time(const suscan_analyzer_t *self)
{
  return (self->iface->is_real_time) (self->impl);
}

/*!
 * Get the sample rate of the underlying signal source used by the analyzer
 * \param analyzer a pointer to the analyzer object
 * \return the sample rate of the signal source
 * \author Gonzalo José Carracedo Carballal
 */
SUINLINE unsigned int
suscan_analyzer_get_samp_rate(const suscan_analyzer_t *self)
{
  return (self->iface->get_samp_rate) (self->impl);
}

/*!
 * Get the actual sample rate as measured by the analyzer's source worker
 * \param analyzer a pointer to the analyzer object
 * \return the measured sample rate
 * \author Gonzalo José Carracedo Carballal
 */
SUINLINE SUFLOAT
suscan_analyzer_get_measured_samp_rate(const suscan_analyzer_t *self)
{
  return (self->iface->get_measured_samp_rate) (self->impl);
}

/*!
 * Get the last reported signal source time
 * \param analyzer a pointer to the analyzer object
 * \param[out] tv timeval struct with the source time
 * \author Gonzalo José Carracedo Carballal
 */
SUINLINE void
suscan_analyzer_get_source_time(
    const suscan_analyzer_t *self, 
    struct timeval *tv)
{
  (self->iface->get_source_time) (self->impl, tv);
}

/*!
 * Requests setting the sample position of the signal source, 
 * in case it is seekable (e.g a raw IQ file). 
 * \param analyzer a pointer to the analyzer object
 * \param tv timeval struct with the source position
 * \return SU_TRUE if the request was delivered, SU_FALSE otherwise
 * \author Gonzalo José Carracedo Carballal
 */
SUINLINE SUBOOL
suscan_analyzer_seek(
    suscan_analyzer_t *self, 
    const struct timeval *tv)
{
  return (self->iface->seek) (self->impl, tv);
}


/*!
 * Return a pointer to the current source information structure. This pointer
 * is analyzer-owned, i.e. the user must not attempt to free it after usage.
 * 
 * NOTE: The source_time field returned by this method must be ignored. Source
 * time is only guaranteed to be meaningful if it was received from a message. 
 * In order to get an updated value of the source time, refer to
 * suscan_analyzer_get_source_time instead.
 * 
 * \see suscan_analyzer_get_source_time
 * 
 * \param analyzer a pointer to the analyzer object
 * \return a pointer to the source information structure
 * \author Gonzalo José Carracedo Carballal
 */
SUINLINE struct suscan_analyzer_source_info *
suscan_analyzer_get_source_info(const suscan_analyzer_t *self)
{
  return (self->iface->get_source_info_pointer) (self->impl);
}

/*!
 * Replace all elements of a complex array by their complex conjugates
 * \param buf pointer to the complex array
 * \param size number of elements in the array
 * \author Gonzalo José Carracedo Carballal
 */
SUINLINE void
suscan_analyzer_do_iq_rev(SUCOMPLEX *buf, SUSCOUNT size)
{
  SUSCOUNT i;
  size <<= 1;
#ifdef _SU_SINGLE_PRECISION
  uint32_t *as_ints = (uint32_t *) buf;
  for (i = 1; i < size; i += 2)
    as_ints[i] ^= 0x80000000;

#else
  uint64_t *as_ints = (uint64_t *) buf;
  for (i = 1; i < size; i += 2)
    as_ints[i] ^= 0x8000000000000000ull;

#endif
}

/*************************** Non-inlined methods ******************************/
/*!
 * In wideband analyzers, set the number of samples to discard from the source
 * after the analyzer has hopped to a new frequency
 * \param self a pointer to the analyzer object
 * \param size number of samples to discard
 * \return SU_TRUE for success or SU_FALSE on failure
 * \author Gonzalo José Carracedo Carballal
 */
SUBOOL suscan_analyzer_set_buffering_size(
    suscan_analyzer_t *self,
    SUSCOUNT size);

/*!
 * In wideband analyzers, set the the boundaries of the frequency sweep
 * interval.
 * \param self a pointer to the analyzer object
 * \param min lower bound of the frequency sweep interval
 * \param max upper bound of the frequency sweep interval
 * \return SU_TRUE for success or SU_FALSE on failure
 * \author Gonzalo José Carracedo Carballal
 */
SUBOOL suscan_analyzer_set_hop_range(
    suscan_analyzer_t *self,
    SUFREQ min,
    SUFREQ max);

/*!
 * In wideband analyzers, set the sweep strategy.
 * \param self a pointer to the analyzer object
 * \param strategy the sweep strategy to use
 * \return SU_TRUE for success or SU_FALSE on failure
 * \author Gonzalo José Carracedo Carballal
 */
SUBOOL suscan_analyzer_set_sweep_stratrgy(
    suscan_analyzer_t *self,
    enum suscan_analyzer_sweep_strategy strategy);

/*!
 * In stochastic-strategy wideband analyzers, set the subset of allowed
 * hop frequencies (also known as partitioning).
 * \see suscan_analyzer_spectrum_partitioning
 * \param self a pointer to the analyzer object
 * \param partitioning spectrum partitioning type
 * \return SU_TRUE for success or SU_FALSE on failure
 * \author Gonzalo José Carracedo Carballal
 */
SUBOOL suscan_analyzer_set_spectrum_partitioning(
    suscan_analyzer_t *self,
    enum suscan_analyzer_spectrum_partitioning partitioning);

/*!
 * Convenience method to set the tuner and LNB frequencies of the signal
 * source used by the analyzer object. The actual frequency tuned by the
 * device is calculated as \param freq - \param lnb
 * \param analyzer a pointer to the analyzer object
 * \param freq new frequency
 * \param lnb LNB frequency
 * \return SU_TRUE for success or SU_FALSE on failure
 * \author Gonzalo José Carracedo Carballal
 */
SUBOOL suscan_analyzer_set_freq(
    suscan_analyzer_t *analyzer,
    SUFREQ freq,
    SUFREQ lnb);

/*!
 * Convenience method to set the signal source's antialias filter bandwidth.
 * Note not all devices support this feature.
 * \param analyzer a pointer to the analyzer object
 * \param bw filter bandwidth in Hz
 * \return SU_TRUE for success or SU_FALSE on failure
 * \author Gonzalo José Carracedo Carballal
 */
SUBOOL suscan_analyzer_set_bw(suscan_analyzer_t *analyzer, SUFLOAT bw);

/*!
 * Convenience method to set the signal source's frequency correction in PPM..
 * Note not all devices support this feature.
 * \param analyzer a pointer to the analyzer object
 * \param ppm frequency correction in parts per million
 * \return SU_TRUE for success or SU_FALSE on failure
 * \author Gonzalo José Carracedo Carballal
 */
SUBOOL suscan_analyzer_set_ppm(suscan_analyzer_t *self, SUFLOAT ppm);

/*!
 * Convenience method to set the gain of a gain element (usually a VGA or an
 * LNB) as exposed by the underlying signal source object.
 * \param analyzer a pointer to the analyzer object
 * \param name name of the gain element
 * \param value gain of the gain element, in dBs
 * \return SU_TRUE for success or SU_FALSE on failure
 * \author Gonzalo José Carracedo Carballal
 */
SUBOOL suscan_analyzer_set_gain(
    suscan_analyzer_t *analyzer,
    const char *name,
    SUFLOAT value);

/*!
 * Convenience method to select the signal source's antenna. Note not all
 * devices allow antenna configuration.
 * \param analyzer a pointer to the analyzer object
 * \param name antenna to use
 * \return SU_TRUE for success or SU_FALSE on failure
 * \author Gonzalo José Carracedo Carballal
 */
SUBOOL suscan_analyzer_set_antenna(
    suscan_analyzer_t *analyzer,
    const char *name);

/*!
 * Convenience method to enable or disable signal source's DC removal
 * \param analyzer a pointer to the analyzer object
 * \param val SU_TRUE to enable DC removal, SU_FALSE otherwise
 * \return SU_TRUE for success or SU_FALSE on failure
 * \author Gonzalo José Carracedo Carballal
 */
SUBOOL suscan_analyzer_set_dc_remove(suscan_analyzer_t *analyzer, SUBOOL val);

/*!
 * Convenience method to enable or disable baseband I/Q reversal (effectively
 * reversing the frequency spectrum)
 * \param analyzer a pointer to the analyzer object
 * \param val SU_TRUE to enable I/Q reversal, SU_FALSE otherwise
 * \return SU_TRUE for success or SU_FALSE on failure
 * \author Gonzalo José Carracedo Carballal
 */
SUBOOL suscan_analyzer_set_iq_reverse(suscan_analyzer_t *analyzer, SUBOOL val);

/*!
 * Convenience method to enable or disable hardware-based automatic gain
 * control (AGC). Note not all devices support this feature.
 * \param analyzer a pointer to the analyzer object
 * \param val SU_TRUE to enable automatic gain control, SU_FALSE otherwise
 * \return SU_TRUE for success or SU_FALSE on failure
 * \author Gonzalo José Carracedo Carballal
 */
SUBOOL suscan_analyzer_set_agc(suscan_analyzer_t *analyzer, SUBOOL val);

/*!
 * Read a message sent by the analyzer object, blocking until a message is
 * received. Equivalent to suscan_analyzer_read_timeout(analyzer, type, NULL).
 * \param analyzer a pointer to the analyzer object
 * \param[out] type pointer to the message type.
 * \return pointer to the message object or NULL on failure. It must be freed
 * using suscan_analyzer_dispose_message
 * \see suscan_analyzer_dispose_message
 * \see suscan_analyzer_read_timeout
 * \author Gonzalo José Carracedo Carballal
 */
void *suscan_analyzer_read(suscan_analyzer_t *analyzer, uint32_t *type);

/*!
 * Read a message sent by the analyzer object, blocking until a message is
 * received or the timeout has expired, whatever happens first.
 * \param analyzer a pointer to the analyzer object
 * \param[out] type pointer to the message type. If the method returns due to
 * a timeout, the value pointed by this pointer remains untouched.
 * \param timeout pointer to the timeval struct with the read timeout. A timeout
 * of 0.000000 seconds returns immediately if no pending messages are present.
 * Passing a NULL pointer waits indefinitely until a message is present.
 * \return pointer to the message object or NULL on failure or timeout. It must
 * be freed using suscan_analyzer_dispose_message
 * \see suscan_analyzer_dispose_message
 * \author Gonzalo José Carracedo Carballal
 */
void *suscan_analyzer_read_timeout(
    suscan_analyzer_t *analyzer,
    uint32_t *type,
    const struct timeval *timeout);

/*!
 * Read the next inspector-type message sent by the analyzer object, blocking
 * until a message is received. This is a convenience method provided in case
 * the user wants to process inspector messages before any other message.
 * \param analyzer a pointer to the analyzer object
 * \param[out] type pointer to the message type.
 * \return pointer to the inspector message object or NULL on failure. It must
 * be freed using suscan_analyzer_dispose_message passing
 * SUSCAN_ANALYZER_MESSAGE_TYPE_INSPECTOR as message type.
 * \see suscan_analyzer_dispose_message
 * \author Gonzalo José Carracedo Carballal
 */
struct suscan_analyzer_inspector_msg *suscan_analyzer_read_inspector_msg(
    suscan_analyzer_t *analyzer);

/*!
 * Read the next inspector-type message sent by the analyzer object, blocking
 * until a message is received or the timeout has expired, whatever happens
 * first. This is a convenience method provided in case the user wants to
 * process inspector messages before any other message.
 * \param analyzer a pointer to the analyzer object
 * \param[out] type pointer to the message type. If the method returns due to
 * a timeout, the value pointed by this pointer remains untouched.
 * \param timeout pointer to the timeval struct with the read timeout. A timeout
 * of 0.000000 seconds returns immediately if no pending messages are present.
 * Passing a NULL pointer waits indefinitely until a message is present.
 * \return pointer to the inspector message object or NULL on failure or
 * timeout. It must be freed using suscan_analyzer_dispose_message passing
 * SUSCAN_ANALYZER_MESSAGE_TYPE_INSPECTOR as message type.
 * \see suscan_analyzer_dispose_message
 * \author Gonzalo José Carracedo Carballal
 */
struct suscan_analyzer_inspector_msg *suscan_analyzer_read_inspector_msg_timeout(
    suscan_analyzer_t *analyzer,
    const struct timeval *timeout);

/*!
 * Read messages from the output queue until a SOURCE_INFO message is found.
 * This prevents retrieving data from the analyzer before the source has been
 * completely initialized (this is particularly important for remote analyzers)
 * \param analyzer a pointer to the analyzer object
 * \param timeout pointer to the timeval struct with the read timeout. A timeout
 * of 0.000000 seconds returns immediately if no pending messages are present.
 * Passing a NULL pointer waits indefinitely until a message is present.
 * \return SU_TRUE if a SOURCE_INFO message has been received in the specified
 * period of time, SU_FALSE otherwise, or if the analyzer failed to start.
 * \author Gonzalo José Carracedo Carballal
 */
SUBOOL suscan_analyzer_wait_until_ready(
  suscan_analyzer_t *self,
  struct timeval *timeout);

/*!
 * Send a message to the analyzer object. The object is created by the usual
 * message constructors. The ownership of the message object is transferred to
 * the analyzer object if and only if the method succeeds.
 * \param analyzer a pointer to the analyzer object
 * \param type message type
 * \param priv generic pointer to the message object
 * \return SU_TRUE for success or SU_FALSE on failure
 * \author Gonzalo José Carracedo Carballal
 */
SUBOOL suscan_analyzer_write(
    suscan_analyzer_t *analyzer,
    uint32_t type,
    void *priv);

/*!
 * Helper function to consume all analyzer messages stored in a message queue.
 * \param mq pointer to the message queue
 * \author Gonzalo José Carracedo Carballal
 */
void suscan_analyzer_consume_mq(struct suscan_mq *mq);

/*!
 * Helper function to dispose analyzer messages according to their type.
 * \param type message type
 * \param priv generic pointer to the message object
 * \return SU_TRUE for success or SU_FALSE on failure
 * \author Gonzalo José Carracedo Carballal
 */
void suscan_analyzer_dispose_message(uint32_t type, void *ptr);

/*!
 * Force the end-of-source condition in the analyzer, behaving as if the
 * signal source stopped delivering samples.
 * \param self a pointer to the analyzer object
 * \return SU_TRUE for success or SU_FALSE on failure
 * \author Gonzalo José Carracedo Carballal
 */
SUBOOL suscan_analyzer_force_eos(suscan_analyzer_t *self);

/*!
 * Send a halt request to the analyzer object, triggering the ordered
 * stop of all processing workers.
 * \param analyzer a pointer to the analyzer object
 * \author Gonzalo José Carracedo Carballal
 */
void suscan_analyzer_req_halt(suscan_analyzer_t *analyzer);

/*!
 * Helper method to halt a worker and consume all messages produced by
 * it before the completion of the halting process.
 * \param worker pointer to the worker object
 * \author Gonzalo José Carracedo Carballal
 */
SUBOOL suscan_analyzer_halt_worker(suscan_worker_t *worker);

/*!
 * Inspects whether this particular instance of the analyzer object supports
 * baseband filters.
 * \param analyzer a pointer to the analyzer object
 * \return SU_TRUE if the analyzer supports baseband filters, SU_FALSE otherwise.
 * \author Gonzalo José Carracedo Carballal
 */
SUBOOL suscan_analyzer_supports_baseband_filtering(suscan_analyzer_t *analyzer);

/*!
 * Registers a baseband filter given by a processing function and a pointer to
 * private data. The baseband filter is installed with default priority.
 * \param analyzer pointer to the analyzer object
 * \param func pointer to the baseband filter function
 * \param privdata pointer to its private data
 * \return SU_TRUE for success or SU_FALSE on failure
 * \author Gonzalo José Carracedo Carballal
 */
SUBOOL suscan_analyzer_register_baseband_filter(
    suscan_analyzer_t *analyzer,
    suscan_analyzer_baseband_filter_func_t func,
    void *privdata);

/*!
 * Registers a baseband filter given by a processing function, a pointer to
 * private data, and a given priority. Smaller values for the prio means
 * higher precedence in the evaluation of the baseband filters.
 * \param analyzer pointer to the analyzer object
 * \param func pointer to the baseband filter function
 * \param privdata pointer to its private data
 * \param prio priority index or SUSCAN_ANALYZER_BBFILT_PRIO_DEFAULT
 * \return SU_TRUE for success or SU_FALSE on failure
 * \author Gonzalo José Carracedo Carballal
 */
SUBOOL suscan_analyzer_register_baseband_filter_with_prio(
    suscan_analyzer_t *analyzer,
    suscan_analyzer_baseband_filter_func_t func,
    void *privdata,
    int64_t prio);


/************************ Client interface methods ****************************/
/*
 * The following methods are wrappers to the message-based analyzer client
 * interface. These methods compose a message object that is delivered to the
 * analyzer object which processes them and _potentially_ delivers a response
 * message.
 *
 * Asynchronous methods return as soon as the underlying request message has
 * been queued, regardless of whether the message has been processed or not.
 */

/*!
 * Requests the setting of the analyzer parameters (asynchronous).
 * \param analyzer pointer to the analyzer object
 * \param params pointer to the new analyzer parameters
 * \param req_id arbitrary request identifier used to match responses
 * \return SU_TRUE for success or SU_FALSE on failure
 * \author Gonzalo José Carracedo Carballal
 */
SUBOOL suscan_analyzer_set_params_async(
    suscan_analyzer_t *analyzer,
    const struct suscan_analyzer_params *params,
    uint32_t req_id);

/*!
 * For throttled sources (e.g. file replay), sets the effective sample rate
 * at which samples are delivered to the analyzer object (asynchronous).
 * \param analyzer pointer to the analyzer object
 * \param samp_rate effective sample rate
 * \param req_id arbitrary request identifier used to match responses
 * \return SU_TRUE for success or SU_FALSE on failure
 * \author Gonzalo José Carracedo Carballal
 */
SUBOOL suscan_analyzer_set_throttle_async(
    suscan_analyzer_t *analyzer,
    SUSCOUNT samp_rate,
    uint32_t req_id);

/*!
 * For seekable sources (e.g. file replay), sets the current read position
 * \param analyzer pointer to the analyzer object
 * \param pos timeval struct with the absolute position in time
 * \param req_id arbitrary request identifier used to match responses
 * \return SU_TRUE for success or SU_FALSE on failure
 * \author Gonzalo José Carracedo Carballal
 */
SUBOOL suscan_analyzer_seek_async(
    suscan_analyzer_t *analyzer,
    const struct timeval *pos,
    uint32_t req_id);

/*!
 * For channel analyzers, open a new inspector of a given class at a given
 * frequency (asynchronous).
 * \param analyzer pointer to the analyzer object
 * \param classname inspector class name
 * \param channel pointer to the channel structure describing the inspector
 * frequency and bandwidth
 * \param precise whether to use precise channel centering
 * \param parent parent inspector (for subcarrier inspection)
 * \param req_id arbitrary request identifier used to match responses
 * \return SU_TRUE for success or SU_FALSE on failure
 * \author Gonzalo José Carracedo Carballal
 */
SUBOOL suscan_analyzer_open_ex_async(
    suscan_analyzer_t *analyzer,
    const char *classname,
    const struct sigutils_channel *channel,
    SUBOOL precise,
    SUHANDLE parent,
    uint32_t req_id);

/*!
 * For channel analyzers, open a new inspector of a given class at a given
 * frequency (asynchronous). Equivalent to suscan_analyzer_open_ex_async(
 * analyzer, classname, channel, SU_FALSE, req_id)
 * \param analyzer pointer to the analyzer object
 * \param classname inspector class name
 * \param channel pointer to the channel structure describing the inspector
 * frequency and bandwidth
 * \param req_id arbitrary request identifier used to match responses
 * \return SU_TRUE for success or SU_FALSE on failure
 * \author Gonzalo José Carracedo Carballal
 */
SUBOOL suscan_analyzer_open_async(
    suscan_analyzer_t *analyzer,
    const char *classname,
    const struct sigutils_channel *channel,
    uint32_t req_id);

/*!
 * For channel analyzers, open a new inspector of a given class at a given
 * frequency. The function returns as soon as the analyzer object has
 * processed the request and delivered a state message.
 * \param analyzer pointer to the analyzer object
 * \param classname inspector class name
 * \param channel pointer to the channel structure describing the inspector
 * frequency and bandwidth
 * \return handle of the new inspector or -1 on error
 * \author Gonzalo José Carracedo Carballal
 */
SUHANDLE suscan_analyzer_open(
    suscan_analyzer_t *analyzer,
    const char *classname,
    const struct sigutils_channel *channel);

/*!
 * For channel analyzers, close an inspector given by its handle (asynchronous).
 * \param analyzer pointer to the analyzer object
 * \param handle inspector handle
 * \param req_id arbitrary request identifier used to match responses
 * \return SU_TRUE for success or SU_FALSE on failure
 * \author Gonzalo José Carracedo Carballal
 */
SUBOOL suscan_analyzer_close_async(
    suscan_analyzer_t *analyzer,
    SUHANDLE handle,
    uint32_t req_id);

/*!
 * For channel analyzers, close an inspector given by its handle. The function
 * returns as soon as the analyzer object has processed the request and
 * delivered a state message.
 * \param analyzer pointer to the analyzer object
 * \param handle inspector handle
 * \return SU_TRUE for success or SU_FALSE on failure
 * \author Gonzalo José Carracedo Carballal
 */
SUBOOL suscan_analyzer_close(
    suscan_analyzer_t *analyzer,
    SUHANDLE handle);

/*!
 * For channel analyzers, set a user-defined 32-bit integer identifier to an
 * inspector given by its handle
 * \param analyzer pointer to the analyzer object
 * \param handle inspector handle
 * \param inspector_id user-defined identifier
 * \param req_id arbitrary request identifier used to match responses
 * \return SU_TRUE for success or SU_FALSE on failure
 * \author Gonzalo José Carracedo Carballal
 */
SUBOOL
suscan_analyzer_set_inspector_id_async(
    suscan_analyzer_t *analyzer,
    SUHANDLE handle,
    uint32_t inspector_id,
    uint32_t req_id);

/*!
 * For channel analyzers, set a user-defined 32-bit integer identifier to an
 * inspector given by its handle (asynchronous).
 * \param analyzer pointer to the analyzer object
 * \param handle inspector handle
 * \param inspector_id user-defined identifier
 * \param req_id arbitrary request identifier used to match responses
 * \return SU_TRUE for success or SU_FALSE on failure
 * \author Gonzalo José Carracedo Carballal
 */
SUBOOL suscan_analyzer_set_inspector_config_async(
    suscan_analyzer_t *analyzer,
    SUHANDLE handle,
    const suscan_config_t *config,
    uint32_t req_id);

/*!
 * For channel analyzers, set the channel center frequency of an inspector
 * (asynchronous).
 * \param analyzer pointer to the analyzer object
 * \param handle inspector handle
 * \param freq new center frequency
 * \param req_id arbitrary request identifier used to match responses
 * \return SU_TRUE for success or SU_FALSE on failure
 * \author Gonzalo José Carracedo Carballal
 */
SUBOOL suscan_analyzer_set_inspector_freq_async(
    suscan_analyzer_t *analyzer,
    SUHANDLE handle,
    SUFREQ freq,
    uint32_t req_id);

/*!
 * For channel analyzers, set the channel filter bandwidth of an inspector
 * (asynchronous).
 * \param analyzer pointer to the analyzer object
 * \param handle inspector handle
 * \param bw filter bandwidth in Hz
 * \param req_id arbitrary request identifier used to match responses
 * \return SU_TRUE for success or SU_FALSE on failure
 * \author Gonzalo José Carracedo Carballal
 */
SUBOOL
suscan_analyzer_set_inspector_bandwidth_async(
    suscan_analyzer_t *analyzer,
    SUHANDLE handle,
    SUFREQ bw,
    uint32_t req_id);

/*!
 * For channel analyzers, set the channel center frequency of an inspector
 * (overridable request).
 * \param analyzer pointer to the analyzer object
 * \param handle inspector handle
 * \param freq new center frequency
 * \return SU_TRUE for success or SU_FALSE on failure
 * \author Gonzalo José Carracedo Carballal
 */
SUBOOL suscan_analyzer_set_inspector_freq_overridable(
    suscan_analyzer_t *analyzer,
    SUHANDLE handle,
    SUFREQ freq);

/*!
 * For channel analyzers, set the channel filter bandwidth of an inspector
 * (overridable request)
 * \param analyzer pointer to the analyzer object
 * \param handle inspector handle
 * \param bw filter bandwidth in Hz
 * \return SU_TRUE for success or SU_FALSE on failure
 * \author Gonzalo José Carracedo Carballal
 */
SUBOOL suscan_analyzer_set_inspector_bandwidth_overridable(
    suscan_analyzer_t *self,
    SUHANDLE handle,
    SUFLOAT bw);

/*!
 * For channel analyzers, set the sampler watermark of an inspector, i.e. the
 * minimum number of samples that must be stored in a sample batch before
 * delivering a sample batch message to the user.
 * \param analyzer pointer to the analyzer object
 * \param handle inspector handle
 * \param watermark new watermark in samples
 * \param req_id arbitrary request identifier used to match responses
 * \return SU_TRUE for success or SU_FALSE on failure
 * \author Gonzalo José Carracedo Carballal
 */
SUBOOL suscan_analyzer_set_inspector_watermark_async(
    suscan_analyzer_t *analyzer,
    SUHANDLE handle,
    SUSCOUNT watermark,
    uint32_t req_id);

/*!
 * For channel analyzer, enable or disable a channel parameter estimator
 * associated to an inspector (asynchronous).
 * \param analyzer pointer to the analyzer object
 * \param handle inspector handle
 * \param estimator_id estimator index as found in the inspector message
 * \param enabled SU_TRUE if the estimator should be running, SU_FALSE otherwise
 * \param req_id arbitrary request identifier used to match responses
 * \return SU_TRUE for success or SU_FALSE on failure
 * \author Gonzalo José Carracedo Carballal
 */
SUBOOL suscan_analyzer_inspector_estimator_cmd_async(
    suscan_analyzer_t *analyzer,
    SUHANDLE handle,
    uint32_t estimator_id,
    SUBOOL   enabled,
    uint32_t req_id);

/*!
 * For channel analyzers, enable or disable periodic updates of a spectrum
 * source associated to an inspector (asynchronous).
 * \param analyzer pointer to the analyzer object
 * \param handle inspector handle
 * \param spectsrc_id spectrum source index as found in the inspector message,
 * -1 to disable
 * \param req_id arbitrary request identifier used to match responses
 * \return SU_TRUE for success or SU_FALSE on failure
 * \author Gonzalo José Carracedo Carballal
 */
SUBOOL suscan_analyzer_inspector_set_spectrum_async(
    suscan_analyzer_t *analyzer,
    SUHANDLE handle,
    uint32_t spectsrc_id,
    uint32_t req_id);

/*!
 * For channel analyzers, configure the Doppler correction of a satellital
 * signal by providing the orbital parameters of the source (asynchronous).
 * \param analyzer pointer to the analyzer object
 * \param handle inspector handle
 * \param orbit orbit object describing the orbital parameters of the
 * satellite, NULL to disable
 * \param req_id arbitrary request identifier used to match responses
 * \return SU_TRUE for success or SU_FALSE on failure
 * \author Gonzalo José Carracedo Carballal
 */
SUBOOL suscan_analyzer_inspector_set_tle_async(
    suscan_analyzer_t *analyzer,
    SUHANDLE handle,
    const orbit_t *orbit,
    uint32_t req_id);

/*!
 * For channel analyzers, if the inspector DSP chain contains an equalizer,
 * reset its internal state (asynchronous).
 * \param analyzer pointer to the analyzer object
 * \param handle inspector handle
 * \param req_id arbitrary request identifier used to match responses
 * \return SU_TRUE for success or SU_FALSE on failure
 * \author Gonzalo José Carracedo Carballal
 */
SUBOOL suscan_analyzer_reset_equalizer_async(
    suscan_analyzer_t *analyzer,
    SUHANDLE handle,
    uint32_t req_id);

/***************************** Internal methods ******************************/
const struct suscan_analyzer_interface *
suscan_local_analyzer_get_interface(void);

const struct suscan_analyzer_interface *
suscan_remote_analyzer_get_interface(void);

SUBOOL
suscan_analyzer_message_has_expired(
    suscan_analyzer_t *self,
    void *msg,
    uint32_t type);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _ANALYZER_H */
