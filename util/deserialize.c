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

#define SU_LOG_DOMAIN "object-xml"

#include <sigutils/log.h>
#include "object.h"

#include <libxml/parser.h>
#include <libxml/tree.h>

SUPRIVATE enum suscan_object_type
suscan_object_xmltag_to_type(const char *tagname)
{
  if (strcmp(tagname, "field") == 0)
    return SUSCAN_OBJECT_TYPE_FIELD;
  else if (strcmp(tagname, "object") == 0)
    return SUSCAN_OBJECT_TYPE_OBJECT;
  else if (strcmp(tagname, "object_set") == 0)
    return SUSCAN_OBJECT_TYPE_SET;

  return -1;
}

SUPRIVATE SUBOOL
suscan_object_populate_from_xmlNode(suscan_object_t *object, xmlNode *node)
{
  xmlNode *this;
  suscan_object_t *new = NULL;
  char *attrib = NULL;
  const char *name;
  enum suscan_object_type type;

  for (this = node->children; this != NULL; this = this->next) {
    if (this->type == XML_ELEMENT_NODE) {
      if ((type = suscan_object_xmltag_to_type((const char *) this->name)) == -1) {
        SU_ERROR("Unrecognized tag name `%s'\n", this->name);
        goto fail;
      }

      /* Create object */
      SU_TRYCATCH(new = suscan_object_new(type), goto fail);

      if ((attrib = (char *) xmlGetProp(this, (const xmlChar *) "name")) != NULL) {
        SU_TRYCATCH(suscan_object_set_name(new, attrib), goto fail);
        xmlFree(attrib);
        attrib = NULL;
      }

      if ((attrib = (char *) xmlGetProp(this, (const xmlChar *) "class")) != NULL) {
        SU_TRYCATCH(suscan_object_set_class(new, attrib), goto fail);
        xmlFree(attrib);
        attrib = NULL;
      }

      /* Append to container */
      if (object->type == SUSCAN_OBJECT_TYPE_OBJECT) {
        if ((name = suscan_object_get_name(new)) == NULL) {
          SU_ERROR("Object members must have a name\n");
          goto fail;
        }

        SU_TRYCATCH(suscan_object_set_field(object, name, new), goto fail);
      } else if (object->type == SUSCAN_OBJECT_TYPE_SET) {
        SU_TRYCATCH(suscan_object_set_append(object, new), goto fail);
      }

      /* Populate new object */
      if (type == SUSCAN_OBJECT_TYPE_FIELD) {
        /* Try to find value from member and contents */
        if ((attrib = (char *) xmlGetProp(this, (const xmlChar *) "value")) == NULL)
          attrib = (char *) xmlNodeGetContent(this);

        if (attrib != NULL) {
          SU_TRYCATCH(suscan_object_set_value(new, attrib), goto fail);
          xmlFree(attrib);
          attrib = NULL;
        }
      } else {
        /* Container objects must be populated too */
        SU_TRYCATCH(suscan_object_populate_from_xmlNode(new, this), goto fail);
      }

      /* New object totally parsed. Move to next */
      new = NULL;
    }
  }

  return SU_TRUE;

fail:
  if (new != NULL)
    suscan_object_destroy(new);

  if (attrib != NULL)
    xmlFree(attrib);

  return SU_FALSE;
}

suscan_object_t *
suscan_object_from_xml(const char *url, const void *data, size_t size)
{
  xmlDocPtr doc;
  suscan_object_t *new = NULL;
  xmlNode *root;
  SUBOOL ok = SU_FALSE;

  if (url == NULL)
    url = "memory.xml";

  doc = xmlReadMemory(data, size, url, NULL, 0);
  if (doc == NULL) {
    SU_ERROR("Failed to parse XML document `%s'\n", url);
    goto done;
  }

  root = xmlDocGetRootElement(doc);
  if (root == NULL) {
    SU_ERROR("XML document `%s' is empty\n", url);
    goto done;
  }

  if (strcmp((const char *) root->name, "serialization") != 0) {
    SU_ERROR("Unexpected root tag `%s' in `%s'\n", root->name, url);
    goto done;
  }

  SU_TRYCATCH(new = suscan_object_new(SUSCAN_OBJECT_TYPE_SET), goto done);

  SU_TRYCATCH(suscan_object_populate_from_xmlNode(new, root), goto done);

  ok = SU_TRUE;

done:
  if (!ok) {
    if (new != NULL) {
      suscan_object_destroy(new);
      new = NULL;
    }
  }

  if (doc != NULL)
    xmlFreeDoc(doc);

  return new;
}

SUBOOL
suscan_object_xml_init(void)
{
  LIBXML_TEST_VERSION;

  return SU_TRUE;
}

void
suscan_object_xml_finalize(void)
{
  xmlCleanupParser();
}

