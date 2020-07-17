/*
 * suscan.h: headers, prototypes and declarations for suscan
 * Creation date: Fri Feb  3 19:41:40 2017
 */

#ifndef _MAIN_INCLUDE_H
#define _MAIN_INCLUDE_H

#include <util.h> /* From util: Common utility library */

#include <analyzer/source.h> /* Generic source API */
#include <analyzer/xsig.h>   /* File sources */
#include <analyzer/mq.h>     /* Message queue object */
#include <analyzer/analyzer.h>

#include <analyzer/msg.h>    /* Suscan-specific messages */

#define SUSCAN_SOURCE_DIALOG_MAX_WIDGET_WIDTH 15
#define SUSCAN_SOURCE_DIALOG_MAX_BASENAME     SUSCAN_SOURCE_DIALOG_MAX_WIDGET_WIDTH
#define SUSCAN_SOURCE_DIALOG_X_PADDING        5
#define SUSCAN_SOURCE_DIALOG_Y_PADDING        7
#define SUSCAN_SOURCE_DIALOG_FIELD_Y_OFFSET   4

#define ARRAY_SZ(arr) ((sizeof(arr)) / sizeof(arr[0]))

#define SUSCAN_SOURCE_TYPE_BLADE_RF ((void *) 1)
#define SUSCAN_SOURCE_TYPE_HACK_RF  ((void *) 2)
#define SUSCAN_SOURCE_TYPE_IQ_FILE  ((void *) 3)
#define SUSCAN_SOURCE_TYPE_WAV_FILE ((void *) 4)
#define SUSCAN_SOURCE_TYPE_ALSA     ((void *) 5)

#define SUSCAN_MANDATORY(expr)          \
  if (!(expr)) {                        \
    fprintf(                            \
      stderr,                           \
      "%s: operation \"%s\" failed\r\n",\
      __FUNCTION__,                     \
      STRINGIFY(expr));                 \
      return SU_FALSE;                  \
  }

enum suscan_mode {
  SUSCAN_MODE_DELAYED_LOG,
  SUSCAN_MODE_IMMEDIATE
};

SUBOOL suscan_channel_is_dc(const struct sigutils_channel *ch);

void suscan_channel_list_sort(
    struct sigutils_channel **list,
    unsigned int count);

char *suscan_log_get_last_messages(struct timeval since, unsigned int max);

SUBOOL suscan_sigutils_init(enum suscan_mode mode);

SUBOOL suscan_gui_start(
    int argc,
    char **argv,
    struct suscan_source_config **config_list,
    unsigned int config_count);

SUBOOL suscan_perform_fingerprint(struct suscan_source_config *config);

#endif /* _MAIN_INCLUDE_H */
