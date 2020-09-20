/*

  Copyright (C) 2017 Gonzalo José Carracedo Carballal

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

#ifndef _ANALYZER_H
#define _ANALYZER_H

#include <sigutils/sigutils.h>
#include <sigutils/detect.h>
#include <pthread.h>

#define _COMPAT_BARRIERS
#include <compat.h>

#include "worker.h"
#include "source.h"
#include "throttle.h"
#include "inspector/inspector.h"
#include "inspsched.h"
#include "mq.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define SUSCAN_ANALYZER_GUARD_BAND_PROPORTION 1.5
#define SUSCAN_ANALYZER_FS_MEASURE_INTERVAL   1.0
#define SUSCAN_ANALYZER_READ_SIZE             512
#define SUSCAN_ANALYZER_MIN_POST_HOP_FFTS     7

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
struct suscan_analyzer_params {
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
  SU_ADDSFX(.1),                                /* channel_update_int */    \
  SU_ADDSFX(.04),                               /* psd_update_int */        \
  0,                                            /* min_freq */              \
  0,                                            /* max_freq */              \
}

/*!
 * \brief Function pointer to baseband filter
 *
 * Convenience typedef of the prototype of baseband filter functions
 * \author Gonzalo José Carracedo Carballal
 */
typedef SUBOOL (*suscan_analyzer_baseband_filter_func_t) (
      void *privdata,
      struct suscan_analyzer *analyzer,
      const SUCOMPLEX *samples,
      SUSCOUNT length);

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

struct suscan_analyzer_gain_request {
  char *name;
  SUFLOAT value;
};

struct suscan_inspector_overridable_request
{
  suscan_inspector_t *insp;

  SUBOOL  dead;
  SUBOOL  freq_request;
  SUFREQ  new_freq;
  SUBOOL  bandwidth_request;
  SUFLOAT new_bandwidth;

  struct suscan_inspector_overridable_request *next;
};

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

struct suscan_analyzer {
  struct suscan_analyzer_params params;
  struct suscan_mq mq_in;   /* To-thread messages */
  struct suscan_mq *mq_out; /* From-thread messages */
  SUBOOL running;
  SUBOOL halt_requested;
  SUBOOL eos;

  /* Source members */
  suscan_source_t *source;
  SUBOOL loop_init;
  pthread_mutex_t loop_mutex;
  suscan_throttle_t throttle; /* For non-realtime sources */
  SUBOOL throttle_mutex_init;
  pthread_mutex_t throttle_mutex;
  SUSCOUNT effective_samp_rate; /* Used for GUI */
  SUFLOAT  measured_samp_rate; /* Used for statistics */
  SUSCOUNT measured_samp_count;
  uint64_t last_measure;
  SUBOOL   iq_rev;

  /* Periodic updates */
  SUFLOAT  interval_channels;
  SUFLOAT  interval_psd;
  SUSCOUNT det_count;
  SUSCOUNT det_num_psd;

  /* This mutex shall protect hot-config requests */
  /* XXX: This is cumbersome. Create a hotconf object to handle these things */
  pthread_mutex_t hotconf_mutex;

  /* Frequency request */
  SUBOOL freq_req;
  SUFREQ freq_req_value;
  SUFREQ lnb_req_value;

  /* XXX: Define list for inspector frequency set */
  SUBOOL   inspector_freq_req;
  SUHANDLE inspector_freq_req_handle;
  SUFREQ   inspector_freq_req_value;

  SUBOOL   inspector_bw_req;
  SUHANDLE inspector_bw_req_handle;
  SUFLOAT  inspector_bw_req_value;

  /* Bandwidth request */
  SUBOOL  bw_req;
  SUFLOAT bw_req_value;

  /* Gain request */
  SUBOOL gain_req_mutex_init;
  PTR_LIST(struct suscan_analyzer_gain_request, gain_request);

  /* Atenna request */
  char *antenna_req;

  /* Usage statistics (CPU, etc) */
  SUFLOAT cpu_usage;
  uint64_t read_start;
  uint64_t process_start;
  uint64_t process_end;
  uint64_t last_psd;
  uint64_t last_channels;

  /* Source worker objects */
  su_channel_detector_t *detector; /* Channel detector */
  suscan_worker_t *source_wk; /* Used by one source only */
  suscan_worker_t *slow_wk; /* Worker for slow operations */
  SUCOMPLEX *read_buf;
  SUSCOUNT   read_size;
  PTR_LIST(struct suscan_analyzer_baseband_filter, bbfilt);

  /* Spectral tuner */
  su_specttuner_t    *stuner;

  /* Wide sweep parameters */
  SUBOOL sweep_params_requested;
  struct suscan_analyzer_sweep_params current_sweep_params;
  struct suscan_analyzer_sweep_params pending_sweep_params;
  SUFREQ   curr_freq;
  SUSCOUNT part_ndx;
  SUSCOUNT fft_samples; /* Number of FFT frames */

  /* Inspector objects */
  PTR_LIST(suscan_inspector_t, inspector); /* This list owns inspectors */
  pthread_mutex_t     inspector_list_mutex; /* Inspector list lock */
  SUBOOL                inspector_list_init;
  suscan_inspsched_t *sched; /* Inspector scheduler */
  pthread_mutex_t     sched_lock;
  pthread_barrier_t   barrier; /* Sched barrier */

  struct suscan_inspector_overridable_request *insp_overridable;

  /* Analyzer thread */
  pthread_t thread;
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

/******************************* Inlined methods ******************************/

/*!
 * Is the analyzer running on top of a real-time source?
 * \param analyzer a pointer to the analyzer object
 * \author Gonzalo José Carracedo Carballal
 */
SUINLINE SUBOOL
suscan_analyzer_is_real_time(const suscan_analyzer_t *analyzer)
{
  return suscan_source_get_type(analyzer->source) == SUSCAN_SOURCE_TYPE_SDR;
}

/*!
 * Get the sample rate of the underlying signal source used by the analyzer
 * \param analyzer a pointer to the analyzer object
 * \return the sample rate of the signal source
 * \author Gonzalo José Carracedo Carballal
 */
SUINLINE unsigned int
suscan_analyzer_get_samp_rate(const suscan_analyzer_t *analyzer)
{
  return suscan_source_get_samp_rate(analyzer->source);
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
  return self->measured_samp_rate;
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

/* Internal */
void suscan_analyzer_destroy_slow_worker_data(suscan_analyzer_t *);

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
 * Acquire a pointer to an inspector-specific overridable request.
 * \param self pointer to the analyzer object
 * \param handle inspector handle
 * \return pointer to the overridable request or NULL on failure
 * \author Gonzalo José Carracedo Carballal
 */
struct suscan_inspector_overridable_request *
suscan_analyzer_acquire_overridable(
    suscan_analyzer_t *self,
    SUHANDLE handle);

/*!
 * Release the pointer to an inspector-specific overridable request.
 * \param self pointer to the analyzer object
 * \param rq pointer to the previously-acquired overridable request
 * \return SU_TRUE for success or SU_FALSE on failure
 * \author Gonzalo José Carracedo Carballal
 */
SUBOOL suscan_analyzer_release_overridable(
    suscan_analyzer_t *self,
    struct suscan_inspector_overridable_request *rq);

/*!
 * Get a pointer to the inspector object identified by a handle
 * \param analyzer pointer to the analyzer object
 * \param rq pointer to the previously-acquired overridable request
 * \return SU_TRUE for success or SU_FALSE on failure
 * \author Gonzalo José Carracedo Carballal
 */
suscan_inspector_t *suscan_analyzer_get_inspector(
    const suscan_analyzer_t *analyzer,
    SUHANDLE handle);

/*!
 * Acquires the analyzer's loop mutex
 * \param analyzer pointer to the analyzer object
 * \return SU_TRUE for success or SU_FALSE on failure
 * \author Gonzalo José Carracedo Carballal
 */
SUBOOL suscan_analyzer_lock_loop(suscan_analyzer_t *analyzer);

/*!
 * Releases the analyzer's loop mutex
 * \param analyzer pointer to the analyzer object
 * \author Gonzalo José Carracedo Carballal
 */
void suscan_analyzer_unlock_loop(suscan_analyzer_t *analyzer);

/*!
 * Acquires the analyzer's inspector list mutex
 * \param analyzer pointer to the analyzer object
 * \return SU_TRUE for success or SU_FALSE on failure
 * \author Gonzalo José Carracedo Carballal
 */
SUBOOL suscan_analyzer_lock_inspector_list(suscan_analyzer_t *analyzer);

/*!
 * Releases the analyzer's inspector list mutex
 * \param analyzer pointer to the analyzer object
 * \author Gonzalo José Carracedo Carballal
 */
void suscan_analyzer_unlock_inspector_list(suscan_analyzer_t *analyzer);

/* Internal */
void suscan_analyzer_source_barrier(suscan_analyzer_t *analyzer);

/* Internal */
void suscan_analyzer_enter_sched(suscan_analyzer_t *analyzer);

/* Internal */
void suscan_analyzer_leave_sched(suscan_analyzer_t *analyzer);

/*!
 * Registers a baseband filter given by a processing function and a pointer to
 * private data.
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

/* Internal */
su_specttuner_channel_t *suscan_analyzer_open_channel_ex(
    suscan_analyzer_t *analyzer,
    const struct sigutils_channel *chan_info,
    SUBOOL precise,
    SUBOOL (*on_data) (
        const struct sigutils_specttuner_channel *channel,
        void *privdata,
        const SUCOMPLEX *data, /* This pointer remains valid until the next call to feed */
        SUSCOUNT size),
        void *privdata);

/* Internal */
su_specttuner_channel_t *suscan_analyzer_open_channel(
    suscan_analyzer_t *analyzer,
    const struct sigutils_channel *chan_info,
    SUBOOL (*on_data) (
        const struct sigutils_specttuner_channel *channel,
        void *privdata,
        const SUCOMPLEX *data, /* This pointer remains valid until the next call to feed */
        SUSCOUNT size),
        void *privdata);

/* Internal */
SUBOOL suscan_analyzer_close_channel(
    suscan_analyzer_t *analyzer,
    su_specttuner_channel_t *channel);

/* Internal */
SUBOOL suscan_analyzer_bind_inspector_to_channel(
    suscan_analyzer_t *analyzer,
    su_specttuner_channel_t *channel,
    suscan_inspector_t *insp);

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
 * For channel analyzers, open a new inspector of a given class at a given
 * frequency (asynchronous).
 * \param analyzer pointer to the analyzer object
 * \param classname inspector class name
 * \param channel pointer to the channel structure describing the inspector
 * frequency and bandwidth
 * \param precise whether to use precise channel centering
 * \param req_id arbitrary request identifier used to match responses
 * \return SU_TRUE for success or SU_FALSE on failure
 * \author Gonzalo José Carracedo Carballal
 */
SUBOOL suscan_analyzer_open_ex_async(
    suscan_analyzer_t *analyzer,
    const char *classname,
    const struct sigutils_channel *channel,
    SUBOOL precise,
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

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _ANALYZER_H */
