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

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <net/if.h>
#include <sys/ioctl.h>

#include <sigutils/log.h>
#include "compat.h"

SUPRIVATE struct suscan_nic_info g_nic_info;

uint32_t
suscan_get_nic_addr(const char *name)
{
  unsigned int i;

  if (g_nic_info.nic_count == 0)
    suscan_get_nic_info(&g_nic_info);
  
  for (i = 0; i < g_nic_info.nic_count; ++i)
    if (strcmp(g_nic_info.nic_list[i]->name, name) == 0)
      return g_nic_info.nic_list[i]->s_addr;

  return 0;
}

SUBOOL
suscan_get_nic_info(struct suscan_nic_info *self)
{
  struct ifreq ifc;
  struct suscan_nic *nic = NULL;
  int sfd = -1;
  int res;
  struct if_nameindex *if_nidxs = NULL, *p = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYC(sfd = socket(AF_INET, SOCK_DGRAM, 0));

  memset(self, 0, sizeof(struct suscan_nic_info));
  
  if_nidxs = if_nameindex();

  if (if_nidxs != NULL) {
    /* For every interface */
    for (p = if_nidxs; p->if_index != 0 || p->if_name != NULL; ++p) {
      /* Request info from this interface */
      strcpy(ifc.ifr_name, p->if_name);
      res = ioctl(sfd, SIOCGIFADDR, &ifc);

      if (res >= 0) {
        SU_MAKE(
          nic,
          suscan_nic,
          p->if_name,
          ((struct sockaddr_in *) &ifc.ifr_addr)->sin_addr.s_addr);
        
        SU_TRYC(PTR_LIST_APPEND_CHECK(self->nic, nic));
        
        nic = NULL;
      }
    }
  }
  
  ok = SU_TRUE;

done:
  if (nic != NULL)
    suscan_nic_destroy(nic);

  if (sfd >= 0)
    close(sfd);

  if (if_nidxs != NULL)
    if_freenameindex(if_nidxs);

  if (!ok) {
    suscan_nic_info_finalize(self);
    memset(self, 0, sizeof(struct suscan_nic_info));
  }

  return ok;
}
