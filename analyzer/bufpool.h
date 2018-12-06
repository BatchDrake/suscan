/*

  Copyright (C) 2018 Gonzalo José Carracedo Carballal

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

#ifndef _ANALYZER_BUFPOOL_H
#define _ANALYZER_BUFPOOL_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <sigutils/types.h>
#include <pthread.h>

struct suscan_buffer_header {
  union {
    struct {
      uint16_t pool_index;
      uint16_t length;
    };

    struct suscan_buffer_header *next;
    SUCOMPLEX placeholder;
  };

  SUCOMPLEX data[0];
};


struct suscan_pool {
  struct suscan_buffer_header *first;
  unsigned int allocated;
  pthread_mutex_t mutex;
};

SUINLINE uint16_t
suscan_buffer_get_length(const SUCOMPLEX *data)
{
  struct suscan_buffer_header *header;
  header = (struct suscan_buffer_header *) (
      (char *) data - sizeof(struct suscan_buffer_header));

  return header->length;
}

void suscan_buffer_return(SUCOMPLEX *data);
SUCOMPLEX *suscan_buffer_alloc(unsigned int length);
SUBOOL suscan_init_pools(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _ANALYZER_BUFPOOL_H */
