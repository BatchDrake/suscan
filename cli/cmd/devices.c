/*

  Copyright (C) 2021 Gonzalo José Carracedo Carballal

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

#define SU_LOG_DOMAIN "cli-devices"

#include <analyzer/source.h>
#include <analyzer/analyzer.h>
#include <analyzer/device/properties.h>
#include <analyzer/device/facade.h>
#include <cli/cli.h>
#include <cli/cmds.h>
#include <unistd.h>
#include <stddef.h>

#define SUSCLI_DEVICE_DISCOVERY_TIMEOUT_SEC 2

SUPRIVATE char *
suscli_ellipsis(const char *string, unsigned int size)
{
  char *dup;
  unsigned int middle;
  const char *ellipsis = "(...)";
  unsigned ellipsis_len = strlen(ellipsis);
  unsigned int halflen = ellipsis_len / 2;
  unsigned int full_len = strlen(string);
  unsigned int tail_start;
  unsigned int tail_size;

  if (size <= ellipsis_len)
    return NULL;

  if ((dup = strdup(string)) == NULL)
    return NULL;

  if (full_len > size) {
    middle     = size / 2;
    tail_start = middle - halflen + ellipsis_len;
    tail_size  = size - tail_start;

    memcpy(dup + tail_start - ellipsis_len, ellipsis, ellipsis_len);

    memcpy(
        dup + tail_start,
        string + (full_len - tail_size - 1),
        tail_size);

    dup[size] = '\0';
  }

  return dup;
}

SUPRIVATE SUBOOL
suscli_devices_print_properties(
  int ndx,
  const suscan_device_properties_t *prop)
{
  char *ellipsis = NULL;
  SUBOOL ok = SU_FALSE;
  SU_TRY(ellipsis = suscli_ellipsis(prop->label, 40));

  printf(
      "[%2u] %-40s %-8s %-9s %016" PRIx64 "\n",
      ndx,
      ellipsis,
      prop->source->name,
      prop->analyzer->name,
      prop->uuid);

  ok = SU_TRUE;

done:
  if (ellipsis != NULL)
    free(ellipsis);
  
  return ok;
}

SUPRIVATE SUBOOL
suscli_devices_print_all()
{
  suscan_device_facade_t *facade = NULL;
  suscan_device_properties_t **prop_list = NULL;
  int i, count = 0;
  SUBOOL ok = SU_FALSE;

  SU_TRY(facade = suscan_device_facade_instance());
  SU_TRYC(count = suscan_device_facade_get_all_devices(facade, &prop_list));

  for (i = 0; i < count; ++i)
    suscli_devices_print_properties(i + 1, prop_list[i]);

  ok = SU_TRUE;

done:
  for (i = 0; i < count; ++i)
    if (prop_list[i] != NULL)
      SU_DISPOSE(suscan_device_properties, prop_list[i]);
  if (prop_list != NULL)
    free(prop_list);

  return ok;
}

SUBOOL
suscli_devices_cb(const hashlist_t *params)
{
  SUBOOL ok = SU_FALSE;
  char *dup = NULL;
  unsigned int timeout = SUSCLI_DEVICE_DISCOVERY_TIMEOUT_SEC;
  suscan_device_facade_t *facade = NULL;
  
  SU_TRY(facade = suscan_device_facade_instance());
  SU_TRY(suscan_device_facade_discover_all(facade));

  if (suscan_device_discovery_lookup("multicast") != NULL) {
    fprintf(
      stderr,
      "Waiting %u seconds for multicast discovery to complete...\n",
      timeout);
    sleep(timeout);
    timeout = 0;
  }

  if ((dup = suscan_device_facade_wait_for_devices(facade, timeout * 1000)) != NULL)
    free(dup);
  
  printf(
      " ndx Device name                              Driver   Interface UUID \n");
  printf(
      "---------------------------------------------------------------------------------\n");

  SU_TRY(suscli_devices_print_all());

  ok = SU_TRUE;

done:
  return ok;
}
