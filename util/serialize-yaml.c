/*

  Copyright (C) 2018 Gonzalo José Carracedo Carballal

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

#include <string.h>
#include <ctype.h>

#define SU_LOG_DOMAIN "object-yaml"

#include <sigutils/log.h>
#include <yaml/yaml.h>
#include "object.h"

#define START(arr) &arr[0]
#define END(arr) &arr[sizeof(arr) / sizeof(arr[0])]

#define SUSCAN_YAML_EMIT(name, arg...)                            \
  do {                                                            \
    SU_TRY(JOIN(yaml_, JOIN(name, _event_initialize) (            \
      &event,                                                     \
      ##arg)));                                                   \
    SU_TRY(yaml_emitter_emit(emitter, &event));                   \
  } while (0)

#define SUSCAN_YAML_TAGGED_MAP_START(tag)                         \
  SUSCAN_YAML_EMIT(                                               \
    mapping_start,                                                \
    NULL,                                                         \
    (const yaml_char_t *) tag,                                    \
    tag == NULL,                                                  \
    YAML_BLOCK_MAPPING_STYLE)

#define SUSCAN_YAML_MAP_START() SUSCAN_YAML_TAGGED_MAP_START(NULL)

#define SUSCAN_YAML_MAP_END() SUSCAN_YAML_EMIT(mapping_end)

#define SUSCAN_YAML_SEQ_START()                                   \
  SUSCAN_YAML_EMIT(                                               \
    sequence_start,                                               \
    NULL,                                                         \
    NULL,                                                         \
    0,                                                            \
    YAML_BLOCK_MAPPING_STYLE)

#define SUSCAN_YAML_SEQ_END() SUSCAN_YAML_EMIT(sequence_end)

#define SUSCAN_YAML_TAGGED_SCALAR(tag, name)                      \
  SUSCAN_YAML_EMIT(                                               \
    scalar,                                                       \
    NULL,                                                         \
    (const yaml_char_t *) tag,                                    \
    (const yaml_char_t *) name,                                   \
    -1,                                                           \
    tag == NULL,                                                  \
    tag == NULL,                                                  \
    YAML_ANY_SCALAR_STYLE)

#define SUSCAN_YAML_SCALAR(name) SUSCAN_YAML_TAGGED_SCALAR(NULL, name)

SUPRIVATE SUBOOL
suscan_object_to_yaml_value_internal(
    yaml_emitter_t *emitter,
    const suscan_object_t *object)
{
  yaml_event_t event;
  const suscan_object_t *current;
  char *tag = NULL;
  unsigned int i;
  SUBOOL ok = SU_FALSE;

  if (object->class_name != NULL)
    SU_TRY(tag = strbuild(SUSCAN_YAML_PFX "%s", object->class_name));

  /* Specific serializators */
  switch (object->type) {
    case SUSCAN_OBJECT_TYPE_FIELD:
      if (object->value != NULL)
        SUSCAN_YAML_SCALAR(object->value);
      break;

    case SUSCAN_OBJECT_TYPE_SET:
      SUSCAN_YAML_SEQ_START();
      for (i = 0; i < object->object_count; ++i)
        if (object->object_list[i] != NULL)
          SU_TRY(
              suscan_object_to_yaml_value_internal(
                emitter, 
                object->object_list[i]));
      SUSCAN_YAML_SEQ_END();
      break;

    case SUSCAN_OBJECT_TYPE_OBJECT:  
      SUSCAN_YAML_TAGGED_MAP_START(tag);

      for (i = 0; i < object->field_count; ++i) {
        current = object->field_list[i];
        if (current != NULL  && current->name != NULL) {
          SUSCAN_YAML_SCALAR(current->name);
          SU_TRY(
              suscan_object_to_yaml_value_internal(
                emitter,
                current));
        }
      }

      SUSCAN_YAML_MAP_END();
      break;
  }

  ok = SU_TRUE;

done:
  if (tag != NULL)
    free(tag);

  return ok;
}

SUPRIVATE int
suscan_yaml_append(void *userdata, uint8_t *buf, size_t size)
{
  grow_buf_t *growbuf = (grow_buf_t *) userdata;
  int ok = 0;

  SU_TRYC(grow_buf_append(growbuf, buf, size));

  ok = 1;

done:
  return ok;
}

SUBOOL
suscan_object_to_yaml(const suscan_object_t *object, void **data, size_t *size)
{
  yaml_emitter_t s_emitter;
  yaml_emitter_t *emitter = &s_emitter;
  yaml_event_t event;
  yaml_tag_directive_t tags[] = {
    {
      (yaml_char_t *) "!",
      (yaml_char_t *) SUSCAN_YAML_PFX,
    }
  };
  SUBOOL emitter_initialized = SU_FALSE;
  unsigned int i;
  SUBOOL ok = SU_FALSE;
  grow_buf_t buffer = grow_buf_INITIALIZER;

  SU_TRY(yaml_emitter_initialize(emitter));
  yaml_emitter_set_output(emitter, suscan_yaml_append, &buffer);
  yaml_emitter_set_unicode(emitter, 1);

  emitter_initialized = SU_TRUE;

  SUSCAN_YAML_EMIT(stream_start, YAML_UTF8_ENCODING);
  SUSCAN_YAML_EMIT(document_start, NULL, START(tags), END(tags), SU_TRUE);
  
  SUSCAN_YAML_SEQ_START();

  for (i = 0; i < object->object_count; ++i)
    if (object->object_list[i] != NULL)
      SU_TRY(
        suscan_object_to_yaml_value_internal(emitter, object->object_list[i]));

  SUSCAN_YAML_SEQ_END();
  SUSCAN_YAML_EMIT(document_end, 1);
  SUSCAN_YAML_EMIT(stream_end);

  /* Transfer ownership */
  *data = grow_buf_get_buffer(&buffer);
  *size = grow_buf_get_size(&buffer);
  grow_buf_init(&buffer);

  ok = SU_TRUE;

done:
  grow_buf_finalize(&buffer);

  if (emitter_initialized)
    yaml_emitter_delete(emitter);

  return ok;
}
