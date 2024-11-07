/*

  Copyright (C) 2020 Gonzalo José Carracedo Carballal

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


#ifndef _UTIL_STRMAP_H
#define _UTIL_STRMAP_H

#include <sigutils/types.h>
#include "hashlist.h"
#include <sigutils/defs.h>
#include <analyzer/serialize.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef hashlist_t          strmap_t;
typedef hashlist_iterator_t strmap_iterator_t;

#define strmap_begin            hashlist_begin
#define strmap_iterator_advance hashlist_iterator_advance
#define strmap_iterator_end     hashlist_iterator_end
#define strmap_size             hashlist_size

SUSCAN_TYPE_SERIALIZER_PROTO(strmap);
SUSCAN_TYPE_DESERIALIZER_PROTO(strmap);

SU_CONSTRUCTOR(strmap);
SU_INSTANCER(strmap);

SU_METHOD(strmap, SUBOOL, set, const char *, const char *);
SU_METHOD(strmap, SUBOOL, set_int, const char *, int);
SU_METHOD(strmap, SUBOOL, set_uint, const char *, unsigned int);
SU_METHOD(strmap, SUBOOL, set_asprintf, const char *, const char *, ...);
SU_METHOD(strmap, void,   clear);
SU_METHOD(strmap, SUBOOL, copy, const strmap_t *);
SU_METHOD(strmap, SUBOOL, assign, const strmap_t *);
SU_METHOD(strmap, void,   notify_move);

SU_GETTER(strmap, char **, keys);
SU_GETTER(strmap, const char *, get, const char *);
SU_GETTER(strmap, const char *, get_default, const char *, const char *);
SU_GETTER(strmap, SUBOOL, equals, const strmap_t *);

SU_DESTRUCTOR(strmap);
SU_COLLECTOR(strmap);

#ifdef __cplusplus
}
#endif

#endif /* _UTIL_STRMAP_H */
