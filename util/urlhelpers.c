/*

  Copyright (C) 2024 Gonzalo José Carracedo Carballal

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

#define SU_LOG_DOMAIN "url-helpers"

#include "urlhelpers.h"
#include <sigutils/log.h>
#include <string.h>
#include <ctype.h>

char *
strappend(char *existing, char *fmt, ...)
{
  char *append = NULL;
  char *new_str = NULL;
  va_list ap;

  va_start(ap, fmt);

  SU_TRY(append = vstrbuild(fmt, ap));
  
  if (existing == NULL) {
    new_str = append;
    append  = NULL;
  } else {
    size_t append_len = strlen(append);
    size_t existing_len = strlen(existing);

    SU_ALLOCATE_MANY(new_str, append_len + existing_len + 1, char);
    memcpy(new_str, existing, existing_len);
    memcpy(new_str + existing_len, append, append_len + 1);
    
    free(append);
    append = NULL;

    if (existing != NULL)
      free(existing);
  }

done:
  if (append != NULL)
    free(append);

  va_end(ap);

  return new_str;
}

SUINLINE SUBOOL
suscan_urlencode_is_passthru(char c)
{
  const char *passthru = "_-.~";

  return isalnum(c) || strchr(passthru, c) != NULL;
}

char *
suscan_urlencode(const char *string)
{
  size_t len = strlen(string);
  size_t olen = len;
  size_t alloc = len;
  unsigned int i, p;

  char *tmp;
  char *obuf = NULL;

  SU_ALLOCATE_MANY_FAIL(obuf, alloc + 1, char);

  p = 0;
  for (i = 0; i < len; ++i) {
    if (suscan_urlencode_is_passthru(string[i])) {
      obuf[p++] = string[i];
    } else if (string[i] == ' ') {
      obuf[p++] = '+';
    } else {
      /* Need to escape: increase the output string by 2 */
      olen += 2;
      if (olen > alloc) {
        alloc *= 2;
        SU_TRY_FAIL(tmp = realloc(obuf, alloc + 1));
        obuf = tmp;
      }

      obuf[p++] = '%';
      snprintf(obuf + p, 3, "%02x", (uint8_t) string[i]);
      p += 2;
    }
  }

  obuf[p] = '\0';

  return obuf;

fail:
  if (obuf != NULL)
    free(obuf);

  return NULL;
}

char *
suscan_urldecode(const char *string)
{
  size_t len = strlen(string);
  unsigned int i, p;
  unsigned int byte;
  char *obuf = NULL;

  SU_ALLOCATE_MANY_FAIL(obuf, len + 1, char);

  p = 0;
  for (i = 0; i < len; ++i) {
    if (string[i] == '%') {
      if (i + 3 > len) {
        SU_ERROR("Malformed URI: truncated escape char at the end of the string\n");
        goto fail;
      }
      if (sscanf(string + i + 1, "%02x", &byte) < 1) {
        SU_ERROR("Malformed URI: invalid escape char '%%%c%c'\n", string[i + 1], string[i + 2]);
        goto fail;
      }
      obuf[p++] = (char) byte;
    } else if (string[i] == '+') {
      obuf[p++] = ' ';
    } else {
      obuf[p++] = string[i];
    }
  }

  obuf[p] = '\0';

  return obuf;

fail:
  if (obuf != NULL)
    free(obuf);

  return NULL;
}
