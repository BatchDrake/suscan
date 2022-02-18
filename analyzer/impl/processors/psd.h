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

#ifndef _SUSCAN_CLI_DEVSERV_PROCESSOR_PSD_H
#define _SUSCAN_CLI_DEVSERV_PROCESSOR_PSD_H

#include "../multicast.h"

struct suscli_multicast_processor_psd {
  struct suscli_multicast_processor *proc;
  struct suscan_analyzer_psd_sf_fragment sf_header;
  unsigned int psd_size;
  SUFLOAT     *psd_data;
  unsigned int updates;
};

typedef struct suscli_multicast_processor_psd suscli_multicast_processor_psd_t;

SUBOOL suscli_multicast_processor_psd_register(void);

#endif /* _SUSCAN_CLI_DEVSERV_PROCESSOR_PSD_H */

