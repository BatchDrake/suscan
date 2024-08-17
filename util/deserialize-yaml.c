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

#define SU_LOG_DOMAIN "object-yaml"

#include <sigutils/log.h>
#include <yaml/yaml.h>
#include "object.h"

SUPRIVATE SUBOOL
suscan_object_parse_yaml_event(
  yaml_parser_t *parser,
  yaml_event_t *event,
  suscan_object_t **out);

/* Yaml sequence: process values in a loop */
SUPRIVATE SUBOOL
suscan_object_parse_yaml_sequence(
  yaml_parser_t *parser,
  suscan_object_t *parent)
{
  suscan_object_t *object = NULL;
  yaml_event_t event;
  SUBOOL ok = SU_FALSE;

  memset(&event, 0, sizeof(yaml_event_t));

  for (;;) {
    SU_TRY(yaml_parser_parse(parser, &event));

    if (event.type == YAML_SEQUENCE_END_EVENT)
      break;

    SU_TRY(suscan_object_parse_yaml_event(parser, &event, &object));
    
    if (object != NULL) {
      SU_TRY(suscan_object_set_append(parent, object));
      object = NULL;
    }

    yaml_event_delete(&event);
  }

  ok = SU_TRUE;

done:
  if (object != NULL)
    suscan_object_destroy(object);

  /* Yaml events can be deleted twice */
  yaml_event_delete(&event);

  return ok;
}

SUPRIVATE SUBOOL
suscan_object_parse_yaml_mapping(
  yaml_parser_t *parser,
  suscan_object_t *parent)
{
  suscan_object_t *object = NULL;
  char *name = NULL;
  yaml_event_t event;
  SUBOOL ok = SU_FALSE;

  memset(&event, 0, sizeof(yaml_event_t));

  for (;;) {
    /* Process name */
    SU_TRY(yaml_parser_parse(parser, &event));
    if (event.type == YAML_MAPPING_END_EVENT)
      break;

    if (event.type != YAML_SCALAR_EVENT) {
      SU_ERROR("Mapping: expected scalar key, not %d\n", event.type);
      goto done;
    }

    SU_TRY(name = strdup((char *) event.data.scalar.value));
    yaml_event_delete(&event);

    /* Process value */
    SU_TRY(yaml_parser_parse(parser, &event));
    if (event.type == YAML_MAPPING_END_EVENT)
      break;
    
    SU_TRY(suscan_object_parse_yaml_event(parser, &event, &object));
    
    /* If an object was retrieved, set this field accordingly */
    if (object != NULL) {
      SU_TRY(suscan_object_set_field(parent, name, object));
      object = NULL;
    }

    free(name);
    name = NULL;

    yaml_event_delete(&event);
  }

  ok = SU_TRUE;

done:
  if (object != NULL)
    suscan_object_destroy(object);

  if (name != NULL)
    free(name);

  /* Yaml events can be deleted twice */
  yaml_event_delete(&event);

  return ok;
}

SUPRIVATE SUBOOL
suscan_object_parse_yaml_event(
  yaml_parser_t *parser,
  yaml_event_t  *event,
  suscan_object_t **out)
{
  suscan_object_t *object = NULL;
  SUBOOL ok = SU_FALSE;

  switch (event->type) {
    case YAML_SCALAR_EVENT:
      /* Scalar event: nameless field object */
      if (event->data.scalar.value != NULL) {
        SU_MAKE(object, suscan_object, SUSCAN_OBJECT_TYPE_FIELD);
        SU_TRY(suscan_object_set_value(
          object, 
          (char *) event->data.scalar.value));
      }
      break;

    case YAML_SEQUENCE_START_EVENT:
      /* Sequence start event: nameless sequence object */
      SU_MAKE(object, suscan_object, SUSCAN_OBJECT_TYPE_SET);
      SU_TRY(suscan_object_parse_yaml_sequence(parser, object));
      break;

    case YAML_MAPPING_START_EVENT:
      SU_MAKE(object, suscan_object, SUSCAN_OBJECT_TYPE_OBJECT);

      /* YAML tags are used to encode object classes. */
      if (event->data.mapping_start.tag != NULL) {
        if (memcmp(
          event->data.mapping_start.tag,
          SUSCAN_YAML_PFX,
          sizeof(SUSCAN_YAML_PFX) - 1) == 0) {
          SU_TRY(suscan_object_set_class(
            object,
            (char *) event->data.mapping_start.tag 
              + sizeof(SUSCAN_YAML_PFX) - 1));
        }
      }

      SU_TRY(suscan_object_parse_yaml_mapping(parser, object));
      break;

    default:
      ; /* Do nothing */
  }

  /* Transfer ownership */
  *out = object;
  object = NULL;

  ok = SU_TRUE;

done:
  if (object != NULL)
    suscan_object_destroy(object);

  return ok;
}

suscan_object_t *
suscan_object_from_yaml(const void *data, size_t size)
{
  suscan_object_t *object = NULL;
  yaml_parser_t parser;
  yaml_event_t event;
  SUBOOL parsing = SU_TRUE;
  SUBOOL parser_init = SU_FALSE;
  SUBOOL ok = SU_FALSE;

  SU_TRY(yaml_parser_initialize(&parser));
  yaml_parser_set_input_string(&parser, data, size);
  parser_init = SU_TRUE;

  memset(&event, 0, sizeof(yaml_event_t));

  do {
    yaml_parser_parse(&parser, &event);
    
    if (parser.error != YAML_NO_ERROR) {
      SU_ERROR(
        "YAML parser error %s (line %d): %s\n",
        parser.context,
        parser.mark.line + 1,
        parser.problem);
      goto done;
    }

    if (event.type == YAML_SEQUENCE_START_EVENT) {
      SU_TRY(suscan_object_parse_yaml_event(&parser, &event, &object));
    }

    parsing = event.type != YAML_STREAM_END_EVENT;

    yaml_event_delete(&event);
  } while (object == NULL && parsing);
  
  ok = SU_TRUE;

done:
  if (!ok) {
    if (object != NULL) {
      suscan_object_destroy(object);
      object = NULL;
    }
  }

  yaml_event_delete(&event);

  if (parser_init)
    yaml_parser_delete(&parser);
  
  return object;
}
