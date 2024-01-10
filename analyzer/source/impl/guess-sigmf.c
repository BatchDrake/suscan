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

#include "file.h"
#include <analyzer/source.h>
#include <sigutils/util/compat-time.h>
#include <string.h>
#include <json.h>

struct sigmf_parser_context {
  json_object *root; /* Owned */
  json_object *global;
  json_object *captures;
};

typedef struct sigmf_parser_context sigmf_parser_context_t;

SU_COLLECTOR(sigmf_parser_context)
{
  if (self->root != NULL)
    json_object_put(self->root);

  free(self);
}

SU_METHOD(
  sigmf_parser_context,
  SUBOOL,
  extract,
  struct suscan_sigmf_metadata *metadata)
{
  json_object *datatype      = NULL;
  json_object *sample_rate   = NULL;
  json_object *first_capture = NULL;
  json_object *datetime      = NULL;
  json_object *frequency     = NULL;

  const char *as_str;

  SUBOOL ok = SU_FALSE;

  if (!json_object_object_get_ex(self->global, "core:datatype", &datatype)) {
    SU_ERROR("Undefined SigMF sample datatype\n");
    goto done;
  }

  if (!json_object_is_type(datatype, json_type_string)) {
    SU_ERROR("Invalid SigMF sample datatype\n");
    goto done;
  }

  if (!json_object_object_get_ex(self->global, "core:sample_rate", &sample_rate)) {
    SU_ERROR("Undefined SigMF sample rate\n");
    goto done;
  }

  if (!json_object_is_type(sample_rate, json_type_int)
    && !json_object_is_type(sample_rate, json_type_double)) {
    SU_ERROR("SigMF sample rate is not numeric\n");
    goto done;
  }

  first_capture = json_object_array_get_idx(self->captures, 0);
  if (!json_object_is_type(first_capture, json_type_object)) {
    SU_ERROR("Type of first capture entry is not object\n");
    goto done;
  }

  if (json_object_object_get_ex(first_capture, "core:datetime", &datetime))
    if (!json_object_is_type(datetime, json_type_string))
      datetime = NULL;
  
  if (!json_object_object_get_ex(first_capture, "core:frequency", &frequency)) {
    SU_ERROR("Undefined SigMF capture frequency\n");
    goto done;
  }

  if (!json_object_is_type(frequency, json_type_int)
    && !json_object_is_type(frequency, json_type_double)) {
    SU_ERROR("SigMF frequency is not numeric\n");
    goto done;
  }

  as_str = json_object_get_string(datatype);
  if (strcmp(as_str, "ci8") == 0) {
    metadata->format = SUSCAN_SOURCE_FORMAT_RAW_SIGNED8;
  } else if (strcmp(as_str, "cu8") == 0) {
    metadata->format = SUSCAN_SOURCE_FORMAT_RAW_UNSIGNED8;
  } else if (strcmp(as_str, "ci16_le") == 0) {
    metadata->format = SUSCAN_SOURCE_FORMAT_RAW_SIGNED16;
  } else if (strcmp(as_str, "cf32_le") == 0) {
    metadata->format = SUSCAN_SOURCE_FORMAT_RAW_FLOAT32;
  } else {
    SU_ERROR("Unrecognized sample format `%s'\n", as_str);
    goto done;
  }

  metadata->guessed |= SUSCAN_SOURCE_CONFIG_GUESS_FORMAT;

  metadata->sample_rate = json_object_get_int(sample_rate);
  metadata->guessed |= SUSCAN_SOURCE_CONFIG_GUESS_SAMP_RATE;

  metadata->frequency = json_object_get_double(frequency);
  metadata->guessed |= SUSCAN_SOURCE_CONFIG_GUESS_FREQ;

  if (datetime != NULL) {
    as_str = json_object_get_string(datetime);
    struct tm tt = {0};
	  double seconds;

	  if (sscanf(
      as_str,
      "%04d-%02d-%02dT%02d:%02d:%lfZ",
	    &tt.tm_year, &tt.tm_mon, &tt.tm_mday,
	    &tt.tm_hour, &tt.tm_min, &seconds) == 6) {
      tt.tm_sec   = (int) floor(seconds);
	    tt.tm_mon  -= 1;
	    tt.tm_year -= 1900;
	    tt.tm_isdst = -1;

      metadata->start_time.tv_sec  = mktime(&tt) - timezone;
      metadata->start_time.tv_usec = (seconds - tt.tm_sec) * 1e6;
      metadata->guessed |= SUSCAN_SOURCE_CONFIG_GUESS_IS_UTC;
      metadata->guessed |= SUSCAN_SOURCE_CONFIG_GUESS_START_TIME;
    }
  }

  ok = SU_TRUE;

done:
  return ok;
}

SU_INSTANCER(sigmf_parser_context, const char *path)
{
  sigmf_parser_context_t *new = NULL;

  SU_ALLOCATE_FAIL(new, sigmf_parser_context_t);

  new->root = json_object_from_file(path);
  if (new->root == NULL) {
    SU_ERROR(
      "Cannot parse JSON file `%s:': %s\n",
      new->root,
      json_util_get_last_err());
    goto fail;
  }

  if (!json_object_object_get_ex(new->root, "global", &new->global)) {
    SU_ERROR("Global key missing\n");
    goto fail;
  }

  SU_TRY_FAIL(json_object_is_type(new->global, json_type_object));

  if (!json_object_object_get_ex(new->root, "captures", &new->captures)) {
    SU_ERROR("Captures key missing\n");
    goto fail;
  }
  
  SU_TRY_FAIL(json_object_is_type(new->captures, json_type_array));
  SU_TRY_FAIL(json_object_array_length(new->captures) >= 1);

  return new;

fail:
  if (new != NULL)
    sigmf_parser_context_destroy(new);

  return NULL;
}

void
suscan_sigmf_metadata_finalize(struct suscan_sigmf_metadata *self)
{
  if (self->path_data != NULL)
    free(self->path_data);

  if (self->path_meta != NULL)
    free(self->path_meta);
}

SUBOOL
suscan_sigmf_extract_metadata(
  struct suscan_sigmf_metadata *self,
  const char *path)
{
  sigmf_parser_context_t *ctx = NULL;
  const char *p = NULL;
  SUBOOL ok = SU_FALSE;

  memset(self, 0, sizeof (struct suscan_sigmf_metadata));

  if ((p = strrchr(path, '.')) == NULL)
    goto done;

  if (strcmp(p, ".sigmf-meta") != 0 && strcmp(p, ".sigmf-data") != 0)
    goto done;

  SU_TRY(self->path_data = strdup(path));
  SU_TRY(self->path_meta = strdup(path));

  memcpy(self->path_data + strlen(path) - 4, "data", 4);
  memcpy(self->path_meta + strlen(path) - 4, "meta", 4);

  SU_MAKE(ctx, sigmf_parser_context, self->path_meta);

  if (!sigmf_parser_context_extract(ctx, self)) {
    SU_ERROR("Failed to parse SigMF metadata\n");
    goto done;
  }

  ok = SU_TRUE;

done:
  if (!ok)
    suscan_sigmf_metadata_finalize(self);
  
  if (ctx != NULL)
    sigmf_parser_context_destroy(ctx);
  
  return ok;
}
