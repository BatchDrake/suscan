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

#ifndef _SUSCAN_POOL_H
#define _SUSCAN_POOL_H

#include <sigutils/types.h>
#include <sigutils/defs.h>

#include "mq.h"

struct sigutils_sample_buffer_pool;

struct sigutils_sample_buffer {
  struct sigutils_sample_buffer_pool *parent;
  int        rindex; /* Reverse index in the buffer table */
  SUBOOL     circular;
  SUBOOL     acquired;

  SUCOMPLEX *data;
  SUSCOUNT   size;
};

typedef struct sigutils_sample_buffer su_sample_buffer_t;

struct sigutils_sample_buffer_pool_params {
  SUBOOL vm_circularity;
};

struct sigutils_sample_buffer_pool {
  PTR_LIST(su_sample_buffer_t, buffer);

  
};

typedef struct sigutils_sample_buffer_pool su_sample_buffer_pool_t;

SU_CONSTRUCTOR(su_sample_buffer_pool);
SU_DESTRUCTOR(su_sample_buffer_pool);

SU_INSTANCER(su_sample_buffer_pool);
SU_COLLECTOR(su_sample_buffer_pool);

SU_METHOD(su_sample_buffer_pool, su_sample_buffer_t *, acquire);
SU_METHOD(su_sample_buffer_pool, su_sample_buffer_t *, give);

#endif /* _SUSCAN_POOL */
