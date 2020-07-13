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

#define SU_LOG_DOMAIN "tcp-datasaver"

#include <sigutils/log.h>
#include <cli/datasaver.h>
#include <cli/cli.h>
#include <netdb.h>
#include <sys/socket.h>
#include <errno.h>
#include <time.h>
#include <string.h>

#define SUSCLI_DATASAVER_TCP_DEFAULT_HOST "localhost"
#define SUSCLI_DATASAVER_TCP_DEFAULT_PORT 9999

SUPRIVATE int
suscli_tcp_connect(const char *host, uint16_t port)
{
  int fd = -1;
  struct hostent *host_entry;
  struct sockaddr_in addr;

  if ((host_entry = gethostbyname(host)) == NULL) {
    SU_ERROR("Address resolution of `%s' failed.\n", host);
    goto fail;
  }

  if ((fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    SU_ERROR("Socket creation failed\n");
    goto fail;
  }

  memset(&addr, 0, sizeof(struct sockaddr_in));
  addr.sin_family = AF_INET;
  addr.sin_addr   = *((struct in_addr *) host_entry->h_addr_list[0]);
  addr.sin_port   = htons(port);

  if (connect(
      fd,
      (struct sockaddr *) &addr,
      sizeof (struct sockaddr_in)) == -1) {
    SU_ERROR("Connection to %s:%d failed: %s\n", host, port, strerror(errno));
    goto fail;
  }

  return fd;

fail:
  if (fd != -1)
    close(fd);

  return -1;
}
SUPRIVATE FILE *
suscli_tcp_fopen(const char *host, uint16_t port)
{
  FILE *fp = NULL;
  int fd = -1;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH((fd = suscli_tcp_connect(host, port)) != -1, goto fail);
  SU_TRYCATCH(fp = fdopen(fd, "wb"), goto fail);

  setbuf(fp, NULL);

  ok = SU_TRUE;

fail:
  if (!ok) {
    if (fp != NULL)
      fclose(fp);
    else if (fd != -1)
      close(fd);

    fp = NULL;
  }

  return fp;
}

SUPRIVATE void *
suscli_tcp_datasaver_open_cb(void *userdata)
{
  const char *host = NULL;
  SUFLOAT interval;
  int port;
  FILE *fp;
  const hashlist_t *params = (const hashlist_t *) userdata;

  SU_TRYCATCH(
      suscli_param_read_string(
          params,
          "host",
          &host,
          SUSCLI_DATASAVER_TCP_DEFAULT_HOST),
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

  if (port == 0)
    port = SUSCLI_DATASAVER_TCP_DEFAULT_PORT;

  if ((fp = suscli_tcp_fopen(host, port)) != NULL)
    fprintf(fp, "RATE,%.6f\n", 1e3 / interval);

  return fp;
}

SUPRIVATE SUBOOL
suscli_tcp_datasaver_write_cb(
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
            "%ld,%.6lf,%.9e,%g\n",
            samples[i].timestamp.tv_sec,
            samples[i].timestamp.tv_usec * 1e-6,
            samples[i].value,
            SU_POWER_DB_RAW(samples[i].value)) > 0,
        return SU_FALSE);
  }

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscli_tcp_datasaver_close_cb(void *state)
{
  FILE *fp = (FILE *) state;
  fclose(fp);

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
