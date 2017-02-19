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

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdarg.h>
#include <ctype.h>
#include <libgen.h>
#include <pthread.h>
#include <stdint.h>

#include <pthread.h>
#include "suscan.h"

/* Status message */
void
suscan_worker_status_msg_destroy(struct suscan_worker_status_msg *status)
{
  if (status->err_msg != NULL)
    free(status->err_msg);

  free(status);
}

struct suscan_worker_status_msg *
suscan_worker_status_msg_new(uint32_t code, const char *msg)
{
  char *msg_dup = NULL;
  struct suscan_worker_status_msg *new;

  if (msg != NULL)
    if ((msg_dup = strdup(msg)) == NULL)
      return NULL;

  if ((new = malloc(sizeof(struct suscan_worker_status_msg))) == NULL) {
    if (msg_dup != NULL)
      free(msg_dup);
    return NULL;
  }

  new->err_msg = msg_dup;
  new->code = code;

  return new;
}

/* Channel list */
void
suscan_worker_channel_msg_destroy(struct suscan_worker_channel_msg *msg)
{
  unsigned int i;

  for (i = 0; i < msg->channel_count; ++i)
    if (msg->channel_list[i] != NULL)
      su_channel_destroy(msg->channel_list[i]);

  if (msg->channel_list != NULL)
    free(msg->channel_list);

  free(msg);
}

struct suscan_worker_channel_msg *
suscan_worker_channel_msg_new(struct sigutils_channel **list, unsigned int len)
{
  unsigned int i;
  struct suscan_worker_channel_msg *new = NULL;
  unsigned int n = 0;

  if ((new = calloc(1, sizeof(struct suscan_worker_channel_msg))) == NULL)
    goto fail;

  if (len > 0)
    if ((new->channel_list = calloc(len, sizeof(struct sigutils_channel *)))
        == NULL)
      goto fail;

  new->channel_count = len;

  for (i = 0; i < len; ++i)
    if (list[i] != NULL)
      if (SU_CHANNEL_IS_VALID(list[i]))
        if ((new->channel_list[n++] = su_channel_dup(list[i])) == NULL)
          goto fail;

  new->channel_count = n;

  return new;

fail:
  suscan_worker_channel_msg_destroy(new);

  return NULL;
}

void
suscan_worker_dispose_message(uint32_t type, void *ptr)
{
  switch (type) {
    case SUSCAN_WORKER_MESSAGE_TYPE_SOURCE_INIT:
    case SUSCAN_WORKER_MESSAGE_TYPE_EOS:
      suscan_worker_status_msg_destroy(ptr);
      break;

    case SUSCAN_WORKER_MESSAGE_TYPE_CHANNEL:
      suscan_worker_channel_msg_destroy(ptr);
      break;
  }
}

