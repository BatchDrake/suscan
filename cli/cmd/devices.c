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
#include <cli/cli.h>
#include <cli/cmds.h>
#include <unistd.h>

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
suscli_device_print_cb(
    const suscan_source_device_t *dev,
    unsigned int ndx,
    void *userdata)
{
  char *ellipsis = NULL;
  SUBOOL avail = suscan_source_device_is_available(dev);

  SU_TRYCATCH(
      ellipsis = suscli_ellipsis(suscan_source_device_get_desc(dev), 40),
      return SU_FALSE);

  printf(
      "[%2u] %-40s %-8s %-9s %s\n",
      ndx,
      ellipsis,
      suscan_source_device_get_driver(dev),
      suscan_source_device_is_remote(dev) ? "remote" : "local",
      avail ? "\033[1;32mavailable\033[0m" : "\033[1;31munavailable\033[0m");

  free(ellipsis);

  return SU_TRUE;
}

SUBOOL
suscli_devices_cb(const hashlist_t *params)
{
  SUBOOL ok = SU_FALSE;

  if (getenv("SUSCAN_DISCOVERY_IF") != NULL) {
    fprintf(
        stderr,
        "Leaving 2 seconds grace period to allow remote devices to be discovered\n\n");
    sleep(2);
  }

  printf(
      " ndx Device name                              Driver   Interface Availability \n");
  printf(
      "------------------------------------------------------------------------------\n");

  SU_TRYCATCH(
      suscan_source_device_walk(suscli_device_print_cb, NULL),
      goto done);

  ok = SU_TRUE;

done:
  return ok;
}
