/*
 * suscan.h: headers, prototypes and declarations for suscan
 * Creation date: Fri Feb  3 19:41:40 2017
 */

#ifndef _SUSCAN_SUSCAN_H
#define _SUSCAN_SUSCAN_H

#include <sys/time.h>
#include "sgdp4/sgdp4-types.h"
#include "plugin.h"

#ifdef __cplusplus
extern "C" {
#endif

enum suscan_mode {
  SUSCAN_MODE_DELAYED_LOG,
  SUSCAN_MODE_IMMEDIATE,
  SUSCAN_MODE_NOLOG
};

char *suscan_log_get_last_messages(struct timeval since, unsigned int max);

SUBOOL suscan_sigutils_init(enum suscan_mode mode);

SUBOOL suscan_init_sources(void);
SUBOOL suscan_init_estimators(void);
SUBOOL suscan_init_spectsrcs(void);
SUBOOL suscan_init_inspectors(void);

SUBOOL suscan_get_qth(xyz_t *geo);
void   suscan_set_qth(const xyz_t *geo);

#ifdef __cplusplus
}
#endif

#endif /* _SUSCAN_SUSCAN_H */
