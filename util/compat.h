/*
  
  Copyright (C) 2019 Gonzalo José Carracedo Carballal
  
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

#ifndef _SUSCAN_COMPAT_H
#define _SUSCAN_COMPAT_H

#include <stdint.h>
#include <sigutils/util/util.h>
#include <sigutils/types.h>

#  ifdef _COMPAT_BARRIERS
#    ifdef __APPLE__
#      warning Using custom barriers for MacOS
#      include "macos-barriers.h"
#    else
#      warning Using pthread barriers
#      include <pthread.h>
#    endif /* __APPLE__ */
#  endif /* _COMPAT_BARRIERS */

const char *suscan_bundle_get_confdb_path(void);
const char *suscan_bundle_get_soapysdr_module_path(void);

struct suscan_nic {
  char    *name;
  uint32_t addr;
};

typedef struct suscan_nic suscan_nic_t;

struct suscan_nic_info {
  PTR_LIST(struct suscan_nic, nic);
};

#define suscan_nic_info_INITIALIZZER \
{                                    \
  NULL,                              \
  0                                  \
}

struct suscan_nic *suscan_nic_new(const char *, uint32_t);
void   suscan_nic_destroy(struct suscan_nic *self);

SUBOOL suscan_get_nic_info(struct suscan_nic_info *self);
void suscan_nic_info_finalize(struct suscan_nic_info *self);

uint32_t suscan_get_nic_addr(const char *name);

uint32_t suscan_ifdesc_to_addr(const char *ifdesc);

SUBOOL     suscan_vm_circbuf_allowed(SUSCOUNT);
SUCOMPLEX *suscan_vm_circbuf_new(const char *name, void **state, SUSCOUNT size);
void       suscan_vm_circbuf_destroy(void *state);

#endif /* _SUSCAN_COMPAT_H */

