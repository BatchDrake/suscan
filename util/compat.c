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

#define _COMPAT_BARRIERS


#include <stdlib.h>
#include <string.h>
#include <sigutils/log.h>

#include <sigutils/util/compat-socket.h>
#include <sigutils/util/compat-in.h>
#include <sigutils/util/compat-inet.h>
#include "compat.h"

/* Bundle implementations */
#if defined(__APPLE__)
#  include "macos-barriers.imp.h"
#  include "macos-bundle.imp.h"
#elif defined(_WIN32)
#  include "win32-bundle.imp.h"
#else
const char *
suscan_bundle_get_confdb_path(void)
{
  return NULL; /* No bundle path in the default OS */
}

const char *
suscan_bundle_get_soapysdr_module_path(void)
{
  return NULL; /* No default SoapySDR root in the default OS */
}

#endif /* defined(__APPLE__) */

/* NIC-related implementations */
#if defined(__linux__)
#  include "linux-nic.imp.h"
#else
SUBOOL
suscan_get_nic_info(struct suscan_nic_info *self)
{
  return SU_FALSE;
}

uint32_t
suscan_get_nic_addr(const char *name)
{
  return 0;
}

#endif /* __linux __ */

/*************************** Common methods *******************************/
struct suscan_nic *
suscan_nic_new(const char *name, uint32_t saddr)
{
  struct suscan_nic *new = NULL;

  SU_ALLOCATE_FAIL(new, struct suscan_nic);

  SU_TRY_FAIL(new->name = strdup(name));
  new->addr = saddr;

  return new;

fail:
  if (new != NULL)
    suscan_nic_destroy(new);

  return new;
}

void
suscan_nic_destroy(struct suscan_nic *self)
{
  if (self->name != NULL)
    free(self->name);

  free(self);
}

void
suscan_nic_info_finalize(struct suscan_nic_info *self)
{
  unsigned int i;

  for (i = 0; i < self->nic_count; ++i)
    if (self->nic_list[i] != NULL)
      suscan_nic_destroy(self->nic_list[i]);

  if (self->nic_list != NULL)
    free(self->nic_list);
}

uint32_t
suscan_ifdesc_to_addr(const char *ifdesc)
{
  uint32_t value;

  value = inet_addr(ifdesc);
  if (ntohl(value) != 0xffffffff) {
    /* Does it look like an IP address? */
    return value;
  } else {
    /* Does it look like a network address? */
    if ((value = suscan_get_nic_addr(ifdesc)) != 0)
      return value;
  }

  return htonl(0xffffffff);
}

/******************** VM circularity implementation ***************************/
#if defined(__linux__) || defined(__APPLE__)
#  include "unix-vm-circbuf.imp.h"
#else
SUBOOL
suscan_vm_circbuf_allowed(SUSCOUNT size)
{
  return SU_FALSE;
}

SUCOMPLEX *
suscan_vm_circbuf_new(void **state, SUSCOUNT size)
{
  return NULL;
}

void
suscan_vm_circbuf_destroy(void *state)
{
  /* Do nothing */
}
#endif
