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

#ifndef _SUSCAN_ANALYZER_IMPL_REMOTE_H
#define _SUSCAN_ANALYZER_IMPL_REMOTE_H

#include <analyzer/analyzer.h>
#include <netinet/in.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define SUSCAN_REMOTE_PDU_HEADER_MAGIC             0xf5005ca9
#define SUSCAN_REMOTE_ANALYZER_CONNECT_TIMEOUT_MS       10000
#define SUSCAN_REMOTE_ANALYZER_AUTH_TIMEOUT_MS          10000
#define SUSCAN_REMOTE_ANALYZER_PDU_BODY_TIMEOUT_MS       5000
#define SUSCAN_REMOTE_READ_BUFFER                        1400

struct suscan_analyzer_remote_pdu_header {
  uint32_t magic;
  uint32_t size;
};

enum suscan_analyzer_remote_type {
  SUSCAN_ANALYZER_REMOTE_NONE,
  SUSCAN_ANALYZER_REMOTE_AUTH_INFO,
  SUSCAN_ANALYZER_REMOTE_SOURCE_INFO,
  SUSCAN_ANALYZER_REMOTE_SET_FREQUENCY,
  SUSCAN_ANALYZER_REMOTE_SET_GAIN,
  SUSCAN_ANALYZER_REMOTE_SET_ANTENNA,
  SUSCAN_ANALYZER_REMOTE_SET_BANDWIDTH,
  SUSCAN_ANALYZER_REMOTE_SET_DC_REMOVE,
  SUSCAN_ANALYZER_REMOTE_SET_IQ_REVERSE,
  SUSCAN_ANALYZER_REMOTE_SET_AGC,
  SUSCAN_ANALYZER_REMOTE_FORCE_EOS,
  SUSCAN_ANALYZER_REMOTE_SET_SWEEP_STRATEGY,
  SUSCAN_ANALYZER_REMOTE_SET_SPECTRUM_PARTITIONING,
  SUSCAN_ANALYZER_REMOTE_SET_HOP_RANGE,
  SUSCAN_ANALYZER_REMOTE_SET_BUFFERING_SIZE,
  SUSCAN_ANALYZER_REMOTE_MESSAGE,
  SUSCAN_ANALYZER_REMOTE_REQ_HALT,
};

SUSCAN_SERIALIZABLE(suscan_analyzer_remote_call) {
  uint32_t type;

  union {
    struct suscan_analyzer_source_info source_info;
    struct {
      SUFREQ freq;
      SUFREQ lnb;
    };

    struct {
      char   *name;
      SUFLOAT value;
    } gain;

    char *antenna;
    SUFLOAT bandwidth;
    SUBOOL dc_remove;
    SUBOOL iq_reverse;
    SUBOOL agc;
    uint32_t sweep_strategy;
    uint32_t spectrum_partitioning;
    uint32_t buffering_size;

    struct {
      SUFREQ min;
      SUFREQ max;
    } hop_range;

    struct {
      uint32_t type;
      void *ptr;
    } msg;
  };
};

void suscan_analyzer_remote_call_init(
    struct suscan_analyzer_remote_call *self,
    enum suscan_analyzer_remote_type type);

SUBOOL suscan_analyzer_remote_call_take_source_info(
    struct suscan_analyzer_remote_call *self,
    struct suscan_analyzer_source_info *info);

SUBOOL suscan_analyzer_remote_call_deliver_message(
    struct suscan_analyzer_remote_call *self,
    suscan_analyzer_t *analyzer);

void suscan_analyzer_remote_call_finalize(
    struct suscan_analyzer_remote_call *self);

/*!
 * Cancellable read from a socket
 * \param sfd socket descriptor from which to read
 * \param cancelfd file descriptor to the cancellation pipe
 * \param buffer destination buffer
 * \param size number of bytes to read
 * \param timeout_ms read timeout (in milliseconds)
 * Attempts to fill a buffer of \param size bytes by repeatedly polling on
 * the socket descriptor \param sfd. The read operation can be cancelled by
 * other thread simply by writing a byte in the write end of the pipe specified
 * by cancelfd.
 * \return \param size if the read was successful or [0, \param size) if the
 * socket connection was closed prematurely by the remote peer. If an error
 * occurred, the function returns -1 and errno is set to a descriptive
 * error code.
 * \author Gonzalo José Carracedo Carballal
 */
size_t suscan_remote_read(
    int sfd,
    int cancelfd,
    void *buffer,
    size_t size,
    int timeout_ms);

struct suscan_remote_analyzer_peer_info {
  const char *hostname;
  uint16_t port;
  struct in_addr hostaddr;

  int control_fd;
  int data_fd;

  grow_buf_t read_buffer;
  grow_buf_t write_buffer;
};

struct suscan_remote_analyzer {
  suscan_analyzer_t *parent;

  pthread_mutex_t call_mutex;
  SUBOOL call_mutex_initialized;
  struct suscan_analyzer_source_info source_info;
  struct suscan_analyzer_remote_call call;
  struct suscan_remote_analyzer_peer_info peer;
  struct suscan_mq pdu_queue;

  int cancel_pipe[2];

  SUBOOL    rx_thread_init;
  pthread_t rx_thread;

  SUBOOL    tx_thread_init;
  pthread_t tx_thread;
};

typedef struct suscan_remote_analyzer suscan_remote_analyzer_t;

/* Internal */
struct suscan_analyzer_remote_call *
suscan_remote_analyzer_acquire_call(
    suscan_remote_analyzer_t *self,
    enum suscan_analyzer_remote_type type);

SUBOOL
suscan_remote_analyzer_release_call(
    suscan_remote_analyzer_t *self,
    struct suscan_analyzer_remote_call *call);

SUBOOL
suscan_remote_analyzer_queue_call(
    suscan_remote_analyzer_t *self,
    struct suscan_analyzer_remote_call *call,
    SUBOOL is_control);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _SUSCAN_ANALYZER_IMPL_REMOTE_H */
