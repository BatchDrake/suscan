/*

  Copyright (C) 2018 Gonzalo José Carracedo Carballal

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation, either version 3 of the
  License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this program.  If not, see
  <http://www.gnu.org/licenses/>

*/

#include <string.h>
#include <ctype.h>

#define SU_LOG_DOMAIN "object-xml"

#include <sigutils/log.h>
#include "object.h"

#define SUSCAN_OBJECT_MAX_INLINE 32

#define sosprintf(sos, fmt, arg...) \
  SU_TRYCATCH(                      \
      grow_buf_append_printf(&(sos)->buffer, fmt, ##arg), \
      goto fail)


struct suscan_obj_serialization {
  grow_buf_t buffer;
  unsigned int level;
};

#define suscan_obj_serialization_INITIALIZER { grow_buf_INITIALIZER, 0 }

SUPRIVATE SUBOOL
suscan_object_value_is_inlinable(const char *data)
{
  if (strlen(data) > SUSCAN_OBJECT_MAX_INLINE)
    return SU_FALSE;

  while (*data) {
    if (*data == '"' || isspace(*data) || !isprint(*data))
      return SU_FALSE;

    ++data;
  }

  return SU_TRUE;
}

const char *
suscan_object_type_to_xmltag(enum suscan_object_type type)
{
  switch (type) {
    case SUSCAN_OBJECT_TYPE_FIELD:
      return "field";

    case SUSCAN_OBJECT_TYPE_OBJECT:
      return "object";

    case SUSCAN_OBJECT_TYPE_SET:
      return "object_set";
  }

  return NULL;
}

SUPRIVATE SUBOOL
suscan_object_to_xml_internal(
    struct suscan_obj_serialization *sos,
    const suscan_object_t *object)
{
  unsigned int i;
  unsigned int count = 0;
  const char *tag;

  /* Padding */
  for (i = 0; i < sos->level; ++i)
    SU_TRYCATCH(grow_buf_append(&sos->buffer, "  ", 2), goto fail);

  SU_TRYCATCH(tag = suscan_object_type_to_xmltag(object->type), goto fail);

  sosprintf(sos, "<suscan:%s", tag);

  if (object->name != NULL)
    sosprintf(sos, " name=\"%s\"", object->name);

  if (object->class != NULL)
    sosprintf(sos, " class=\"%s\"", object->class);

  /* Specific serializators */
  switch (object->type) {
    case SUSCAN_OBJECT_TYPE_FIELD:
      if (object->value != NULL) {
        if (suscan_object_value_is_inlinable(object->value)) {
          sosprintf(sos, " value=\"%s\" />\n", object->value);
        } else {
          sosprintf(sos, "><![CADATA[%s]]></suscan:field>\n", object->value);
        }
      } else {
        sosprintf(sos, " />\n");
      }

      break;

    case SUSCAN_OBJECT_TYPE_SET:
      for (i = 0; i < object->object_count; ++i)
        if (object->object_list[i] != NULL) {
          if (count++ == 0)
            sosprintf(sos, ">\n");

          ++sos->level;
          SU_TRYCATCH(sos, object->object_list[i]);
          --sos->level;
        }

      if (count == 0)
        sosprintf(sos, " />\n");

      break;

    case SUSCAN_OBJECT_TYPE_OBJECT:
      for (i = 0; i < object->field_count; ++i)
        if (object->field_list[i] != NULL) {
          if (count++ == 0)
            sosprintf(sos, ">\n");

          ++sos->level;
          SU_TRYCATCH(sos, object->field_list[i]);
          --sos->level;
        }

      if (count == 0)
        sosprintf(sos, " />\n");

      break;
  }

  if (count > 0) {
    for (i = 0; i < sos->level; ++i)
      SU_TRYCATCH(grow_buf_append(&sos->buffer, "  ", 2), goto fail);
    sosprintf(sos, "</suscan:%s>\n", tag);
  }

  return SU_TRUE;

fail:
  return SU_FALSE;
}

SUBOOL
suscan_object_to_xml(const suscan_object_t *object, void **data, size_t *size)
{
  struct suscan_obj_serialization sos = suscan_obj_serialization_INITIALIZER;

  sosprintf(&sos, "<?xml version=\"1.0\" ?>\n\n");

  sosprintf(&sos, "<suscan:serialization ");
  sosprintf(&sos, "xmlns:suscan=\"http://actinid.org/suscan\" name=\"root\">\n");

  ++sos.level;

  SU_TRYCATCH(suscan_object_to_xml_internal(&sos, object), goto fail);

  --sos.level;

  sosprintf(&sos, "<suscan:serialization>\n");

  *data = grow_buf_get_buffer(&sos.buffer);
  *size = grow_buf_get_size(&sos.buffer);

  return SU_TRUE;

fail:
  grow_buf_finalize(&sos.buffer);

  *data = NULL;
  *size = 0;

  return SU_FALSE;
}
