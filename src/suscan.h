/*
 * suscan.h: headers, prototypes and declarations for suscan
 * Creation date: Fri Feb  3 19:41:40 2017
 */

#ifndef _MAIN_INCLUDE_H
#define _MAIN_INCLUDE_H

#include <config.h> /* General compile-time configuration parameters */
#include <util.h> /* From util: Common utility library */

#include <analyzer/source.h> /* Generic source API */
#include <analyzer/xsig.h>   /* File sources */
#include <analyzer/mq.h>     /* Message queue object */
#include <analyzer/analyzer.h>

#include <analyzer/msg.h>    /* Suscan-specific messages */

#include "ctk.h"

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

enum ctk_dialog_response suscan_open_source_dialog(
    struct suscan_source_config **config);

char *suscan_log_get_last_messages(struct timeval since, unsigned int max);

SUBOOL suscan_sigutils_init(void);

#endif /* _MAIN_INCLUDE_H */
