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

#define SU_LOG_DOMAIN "tcp-datasaver"

#include <sigutils/log.h>
#include <cli/datasaver.h>
#include <cli/cli.h>
#include <util/compat-in.h>
#include <util/compat-netdb.h>
#include <util/compat-poll.h>
#include <util/compat-fcntl.h>
#include <util/compat-socket.h>
#include <errno.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>

#ifndef MSG_NOSIGNAL
#  define MSG_NOSIGNAL 0
#endif

#define SUSCLI_DATASAVER_TCP_LOG_DELAY   5

#define SUSCLI_DATASAVER_TCP_DEFAULT_HOST "localhost"
#define SUSCLI_DATASAVER_TCP_DEFAULT_PORT 9999

#define SUSCLI_DATASAVER_HOSTNAME_SZ 256

SUPRIVATE SUBOOL g_have_hostname = SU_FALSE;
SUPRIVATE char  g_hostname[SUSCLI_DATASAVER_HOSTNAME_SZ];

#define BINDING(self)    ((self)->fd == -1)
#define CONNECTING(self) ((self)->fd != -1 && !(self)->write_ready)
#define CONNECTED(self)  ((self)->fd != -1 && (self)->write_ready)

/*
 * State machine goes as this:
 * - BINDING (fd = -1):
 *     There is no socket (yet). 
 * - CONNECTING (fd != -1 && !write_ready)
 *     The socket has been created, but the connection is not quite ready yet.
 * - CONNECTED (fd != -1 && write_ready)
 *     The socket has been created and data can be sent to it. The socket
 *     is back to blocking mode.
 * 
 * 1. The datasaver is always opened in CONNECTING state.
 * 2. Upon write(), we check the state:
 *    - BINDING: time to create a new socket. Transition to CONNECTING,
 *      or throw an exception.
 *    - CONNECTING: check whether the socket is connected. If it is,
 *      transition to CONNECTED. If there has been an error, write a
 *      message or raise an exception.
 *    - CONNECTED: write the samples.
 * 3. If the samples failed to be sent, transition to BINDING and
 *    emit a message.
 */

struct tcp_datasaver {
  char *host;
  uint16_t port;
  char *desc;
  SUFLOAT interval;
  struct timeval last_msg;
  int fd;
  SUBOOL write_ready;
  SUBOOL retry;
};

typedef struct tcp_datasaver tcp_datasaver_t;

const char *
suscli_tcp_get_hostname(void)
{
  if (!g_have_hostname) {
    if (gethostname(g_hostname, SUSCLI_DATASAVER_HOSTNAME_SZ - 1) == -1)
      strncpy(g_hostname, "unknown", SUSCLI_DATASAVER_HOSTNAME_SZ - 1);
    g_have_hostname = SU_TRUE;
  }

  return g_hostname;
}

SU_COLLECTOR(tcp_datasaver)
{
  if (self->host != NULL)
    free(self->host);

  if (self->desc != NULL)
    free(self->desc);
  
  if (self->fd != -1)
    (void) shutdown(self->fd, SHUT_RDWR);

  free(self);
}

SU_INSTANCER(
  tcp_datasaver, 
  const char *desc,
  const char *host, 
  uint16_t port,
  SUFLOAT interval,
  SUBOOL retry)
{
  tcp_datasaver_t *new = NULL;

  SU_ALLOCATE_FAIL(new, tcp_datasaver_t);

  new->retry       = retry;
  new->write_ready = SU_FALSE;
  new->interval    = interval;

  SU_TRY_FAIL(new->host = strdup(host));

  if (desc != NULL)
    SU_TRY_FAIL(new->desc = strdup(desc));

  new->port        = port;
  new->fd          = -1;
  
  return new;

fail:
  if (new != NULL)
    SU_DISPOSE(tcp_datasaver, new);

  return NULL;
}

SUPRIVATE SUBOOL
sockprintf(int fd, const char *fmt, ...)
{
  char *buf = NULL;
  SUBOOL ok = SU_FALSE;
  va_list ap;

  va_start(ap, fmt);

  SU_TRY(buf = vstrbuild(fmt, ap));
  SU_TRY(send(fd, buf, strlen(buf), MSG_NOSIGNAL) == strlen(buf));

  ok = SU_TRUE;

done:
  if (buf != NULL)
    free(buf);
  
  va_end(ap);

  return ok;
}

SU_METHOD(tcp_datasaver, SUBOOL, check_transition)
{
  struct hostent *host_entry;
  struct sockaddr_in addr;
  struct pollfd pfd;
  int so_error;
  socklen_t len;
  int fd = -1;
  int ret;
  SUBOOL log_messages;
  SUBOOL ok = SU_FALSE;
  struct timeval tv, diff;
  
  gettimeofday(&tv, NULL);

  timersub(&tv, &self->last_msg, &diff);

  log_messages = diff.tv_sec >= SUSCLI_DATASAVER_TCP_LOG_DELAY;

  if (log_messages)
    self->last_msg = tv;
  
  if (BINDING(self)) {
    if (log_messages)
      SU_INFO("Resolving %s...\n", self->host);
    
    if ((host_entry = gethostbyname(self->host)) == NULL) {
      SU_ERROR("Address resolution of `%s' failed.\n", self->host);
      goto done;
    }

    if ((fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
      SU_ERROR("Socket creation failed\n");
      goto done;
    }

    SU_TRYC(fcntl(fd, F_SETFL, O_NONBLOCK));

    memset(&addr, 0, sizeof(struct sockaddr_in));
    addr.sin_family = AF_INET;
    addr.sin_addr   = *((struct in_addr *) host_entry->h_addr_list[0]);
    addr.sin_port   = htons(self->port);

    ret = connect(fd, (struct sockaddr *) &addr, sizeof(struct sockaddr_in));
    
    if (ret == 0) {
      self->write_ready = SU_TRUE;
    } else if (ret == -1 && errno != EAGAIN && errno != EINPROGRESS) {
      SU_ERROR("Delayed connection failed: %s\n", strerror(errno));
      goto done;
    }

    self->fd = fd;
    fd = -1;
    ok = SU_TRUE;
  } else if (CONNECTING(self)) {
    pfd.events  = POLLOUT;
    pfd.fd      = self->fd;
    pfd.revents = 0;

    SU_TRYC(ret = poll(&pfd, 1, 0));

    if (pfd.revents & POLLOUT) {
      len = sizeof(so_error);
      SU_TRYC(getsockopt(self->fd, SOL_SOCKET, SO_ERROR, &so_error, &len));

      if (so_error == 0) {
        SU_INFO("Successfully connected to RMS consumer.\n");
        self->write_ready = SU_TRUE;    /* Transition to CONNECTED */
        SU_TRYC(fcntl(self->fd, F_SETFL, 0)); /* Back to blocking mode. */

        SU_TRY(sockprintf(self->fd, "RATE,%.6f\n", 1e3 / self->interval));
        if (self->desc == NULL)
          sockprintf(
              self->fd,
              "DESC,suscli@%s (%d)\n",
              suscli_tcp_get_hostname(),
              getpid());
        else
          sockprintf(
              self->fd,
              "DESC,%s\n",
              self->desc);
      } else {
        if (self->retry) {
          if (log_messages)
            SU_WARNING(
              "Connection failed (%s). Trying again...\n",
              strerror(so_error));
          self->write_ready = SU_FALSE;
          close(self->fd);
          self->fd = -1; /* Transition to BINDING */
        } else {
          SU_ERROR(
            "Connection failed (%s). Datasaver closed.\n",
            strerror(so_error));
          goto done;
        }
      }
    }

    ok = SU_TRUE;
  } else if (CONNECTED(self)) {
    pfd.events = POLLHUP;
	  pfd.revents = 0;

    SU_TRYC(ret = poll(&pfd, 1, 0));

    if (pfd.revents & POLLHUP) {
      if (self->retry) {
        if (log_messages)
          SU_WARNING("Remote connection vanished. Trying again...\n");
        self->write_ready = SU_FALSE;
        close(self->fd);
        self->fd = -1; /* Transition to BINDING */
      } else {
        SU_ERROR("Remote connection vanished. Datasaver closed.\n");
        goto done;
      }
    }

    ok = SU_TRUE;
  }

done:
  if (fd != -1)
    close(fd);

  return ok;
}

SUPRIVATE void *
suscli_tcp_datasaver_open_cb(void *userdata)
{
  const char *host = NULL;
  const char *desc = NULL;
  SUBOOL retry;
  SUFLOAT interval;
  int port;
  const hashlist_t *params = (const hashlist_t *) userdata;

  SU_TRYCATCH(
      suscli_param_read_string(
          params,
          "host",
          &host,
          SUSCLI_DATASAVER_TCP_DEFAULT_HOST),
      return NULL);

  SU_TRYCATCH(
      suscli_param_read_bool(
          params,
          "retry",
          &retry,
          SU_TRUE),
      return NULL);

  SU_TRYCATCH(
      suscli_param_read_int(
          params,
          "port",
          &port,
          SUSCLI_DATASAVER_TCP_DEFAULT_PORT),
      return NULL);

  SU_TRYCATCH(
      suscli_param_read_float(
          params,
          "interval",
          &interval,
          1),
      return NULL);

  SU_TRYCATCH(
      suscli_param_read_string(
          params,
          "desc",
          &desc,
          NULL),
      return NULL);

  if (port == 0)
    port = SUSCLI_DATASAVER_TCP_DEFAULT_PORT;

  return tcp_datasaver_new(desc, host, port, interval, retry);
}

SUPRIVATE SUBOOL
suscli_tcp_datasaver_write_cb(
    void *state,
    const struct suscli_sample *samples,
    size_t length)
{
  tcp_datasaver_t *self = (tcp_datasaver_t *) state;
  struct timeval tv, diff;
  SUBOOL log_messages;
  int i;

  if (!tcp_datasaver_check_transition(self))
    return SU_TRUE;

  gettimeofday(&tv, NULL);
  timersub(&tv, &self->last_msg, &diff);
  log_messages = diff.tv_sec >= SUSCLI_DATASAVER_TCP_LOG_DELAY;

  if (CONNECTED(self)) {
    for (i = 0; i < length; ++i) {
      if (!sockprintf(
        self->fd,
        "%ld,%.6lf,%.9e,%g\n",
        samples[i].timestamp.tv_sec,
        samples[i].timestamp.tv_usec * 1e-6,
        samples[i].value,
        SU_POWER_DB_RAW(samples[i].value))) {
        if (self->retry) {
          if (log_messages)
            SU_WARNING("Failed to send message. Retrying...\n");
          self->write_ready = SU_FALSE;
          close(self->fd);
          self->fd = -1; /* Transition to BINDING */
        } else {
          SU_ERROR("Failed to send RMS message. Closing datasaver.\n");
          return SU_FALSE;
        }
      }
    }
  }
  
  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscli_tcp_datasaver_close_cb(void *state)
{
  tcp_datasaver_t *self = (tcp_datasaver_t *) state;

  tcp_datasaver_destroy(self);

  return SU_TRUE;
}

void
suscli_datasaver_params_init_tcp(
    struct suscli_datasaver_params *self,
    const hashlist_t *params) {
  self->userdata = (void *) params;
  self->open  = suscli_tcp_datasaver_open_cb;
  self->write = suscli_tcp_datasaver_write_cb;
  self->close = suscli_tcp_datasaver_close_cb;
}
