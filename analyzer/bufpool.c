/*
  Copyright (C) 2018 Gonzalo José Carracedo Carballal

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

#define SU_LOG_DOMAIN "bufpool"

#include <sigutils/log.h>

#include "bufpool.h"

#define MIN_POOL 5
#define NUM_POOLS 16

SUPRIVATE struct suscan_pool pools[NUM_POOLS];


void
suscan_buffer_return(SUCOMPLEX *data)
{
  struct suscan_buffer_header *header;
  unsigned int index;

  header = (struct suscan_buffer_header *) (
      (char *) data - sizeof(struct suscan_buffer_header));

  if (header->pool_index >= NUM_POOLS) {
    SU_ERROR("*** INVALID POOL BUFFER RETURN ***\n");
    abort();
  }

  index = header->pool_index;

  pthread_mutex_lock(&pools[index].mutex);
  header->next = pools[index].first;
  pools[index].first = header;
  pthread_mutex_lock(&pools[index].mutex);

}

SUCOMPLEX *
suscan_buffer_alloc(unsigned int length)
{
  unsigned int i = 0;
  struct suscan_buffer_header *header = NULL;

  while ((length >>= 1) != 0)
    ++i;

  if (i < MIN_POOL)
    i = MIN_POOL;

  if (i >= NUM_POOLS) {
    SU_ERROR("Pool allocation of %d samples is too big\n", length);
    return NULL;
  }

  pthread_mutex_lock(&pools[i].mutex);
  header = pools[i].first;
  if (header != NULL)
    pools[i].first = header->next;
  pthread_mutex_unlock(&pools[i].mutex);

  if (header == NULL) {
    SU_TRYCATCH(
        header = malloc(
          sizeof(struct suscan_buffer_header) + (sizeof(SUCOMPLEX) << i)),
        return NULL);
  }

  header->pool_index = i;
  header->length = length;

  return header->data;
}

SUBOOL
suscan_init_pools(void)
{
  unsigned int i;

  for (i = 0; i < NUM_POOLS; ++i) {
    SU_TRYCATCH(
        pthread_mutex_init(&pools[i].mutex, NULL) != -1,
        return SU_FALSE);
  }

  return SU_TRUE;
}
