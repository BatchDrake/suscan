/*

  Copyright (C) 2022 Gonzalo José Carracedo Carballal

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

#ifndef _SUSCAN_CLI_DEVSERV_PROCESSOR_ENCAP_H
#define _SUSCAN_CLI_DEVSERV_PROCESSOR_ENCAP_H

#include "../multicast.h"

struct suscli_multicast_processor_encap {
  struct suscli_multicast_processor *proc;
  uint8_t      sf_id;
  unsigned int pdu_size;
  uint8_t     *pdu_data;
  uint64_t    *pdu_bitmap;
  unsigned int pdu_remaining;
};

typedef struct suscli_multicast_processor_encap suscli_multicast_processor_encap_t;

SUBOOL suscli_multicast_processor_encap_register(void);

#endif /* _SUSCAN_CLI_DEVSERV_PROCESSOR_ENCAP_H */

