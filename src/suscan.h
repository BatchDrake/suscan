/*
 * suscan.h: headers, prototypes and declarations for suscan
 * Creation date: Fri Feb  3 19:41:40 2017
 */

#ifndef _MAIN_INCLUDE_H
#define _MAIN_INCLUDE_H

#include <config.h> /* General compile-time configuration parameters */
#include <util.h> /* From util: Common utility library */
#include <sigutils/sigutils.h>

#include "ctk.h"

#define SUSCAN_SOURCE_DIALOG_MAX_WIDGET_WIDTH 15
#define SUSCAN_SOURCE_DIALOG_MAX_BASENAME     SUSCAN_SOURCE_DIALOG_MAX_WIDGET_WIDTH
#define SUSCAN_SOURCE_DIALOG_X_PADDING        5
#define SUSCAN_SOURCE_DIALOG_Y_PADDING        9
#define SUSCAN_SOURCE_DIALOG_FIELD_Y_OFFSET   4

#define ARRAY_SZ(arr) ((sizeof(arr)) / sizeof(arr[0]))

#define SUSCAN_SOURCE_TYPE_BLADE_RF ((void *) 1)
#define SUSCAN_SOURCE_TYPE_HACK_RF  ((void *) 2)
#define SUSCAN_SOURCE_TYPE_IQ_FILE  ((void *) 3)
#define SUSCAN_SOURCE_TYPE_WAV_FILE ((void *) 4)
#define SUSCAN_SOURCE_TYPE_ALSA     ((void *) 5)

enum suscan_field_type {
  SUSCAN_FIELD_TYPE_STRING,
  SUSCAN_FIELD_TYPE_INTEGER,
  SUSCAN_FIELD_TYPE_FLOAT,
  SUSCAN_FIELD_TYPE_FILE
};

union suscan_field_value {
  uint64_t as_int;
  SUFLOAT  as_float;
  char     as_string[0];
};

struct suscan_field {
  enum suscan_field_type type;
  const char *name;
  const char *desc;
};

struct suscan_source_config;

struct suscan_source {
  const char *name;
  const char *desc;

  PTR_LIST(struct suscan_field, field);

  su_block_t *(*ctor) (const struct suscan_source_config *);
};

struct suscan_source_config {
  const struct suscan_source *source;
  union suscan_field_value **values;
};

struct suscan_source *suscan_source_lookup(const char *name);
struct suscan_source *suscan_source_register(
    const char *name,
    const char *desc,
    su_block_t *(*ctor) (const struct suscan_source_config *));
int suscan_source_lookup_field_id(
    const struct suscan_source *source,
    const char *name);
struct suscan_field *suscan_source_field_id_to_field(
    const struct suscan_source *source,
    int id);
struct suscan_field *suscan_source_lookup_field(
    const struct suscan_source *source,
    const char *name);
SUBOOL suscan_source_add_field(
    struct suscan_source *source,
    enum suscan_field_type type,
    const char *name,
    const char *desc);
void suscan_source_config_destroy(struct suscan_source_config *config);
struct suscan_source_config *suscan_source_config_new(
    const struct suscan_source *source);
SUBOOL suscan_source_config_set_integer(
    struct suscan_source_config *cfg,
    const char *name,
    uint64_t value);
SUBOOL suscan_source_config_set_float(
    struct suscan_source_config *cfg,
    const char *name,
    SUFLOAT value);
SUBOOL suscan_source_config_set_string(
    struct suscan_source_config *cfg,
    const char *name,
    const char *value);
SUBOOL suscan_source_config_set_file(
    struct suscan_source_config *cfg,
    const char *name,
    const char *value);

SUBOOL suscan_wav_source_init(void);
SUBOOL suscan_iqfile_source_init(void);

SUBOOL suscan_init_sources(void);

SUBOOL suscan_open_source_dialog(void);

#endif /* _MAIN_INCLUDE_H */
