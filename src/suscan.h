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

#define ARRAY_SZ(arr) ((sizeof(arr)) / sizeof(arr[0]))

#define SUSCAN_SOURCE_TYPE_BLADE_RF ((void *) 1)
#define SUSCAN_SOURCE_TYPE_HACK_RF  ((void *) 2)
#define SUSCAN_SOURCE_TYPE_IQ_FILE  ((void *) 3)
#define SUSCAN_SOURCE_TYPE_WAV_FILE ((void *) 4)
#define SUSCAN_SOURCE_TYPE_ALSA     ((void *) 5)

SUBOOL suscan_open_source_dialog(void);

#endif /* _MAIN_INCLUDE_H */
