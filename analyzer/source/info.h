/*

  Copyright (C) 2023 Gonzalo José Carracedo Carballal

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


#ifndef _ANALYZER_SOURCE_INFO_H
#define _ANALYZER_SOURCE_INFO_H

#include <analyzer/source/config.h>
#include <analyzer/serialize.h>
#include <sgdp4/sgdp4-types.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Permissions */
#define SUSCAN_ANALYZER_PERM_HALT               (1ull << 0)
#define SUSCAN_ANALYZER_PERM_SET_FREQ           (1ull << 1)
#define SUSCAN_ANALYZER_PERM_SET_GAIN           (1ull << 2)
#define SUSCAN_ANALYZER_PERM_SET_ANTENNA        (1ull << 3)
#define SUSCAN_ANALYZER_PERM_SET_BW             (1ull << 4)
#define SUSCAN_ANALYZER_PERM_SET_PPM            (1ull << 5)
#define SUSCAN_ANALYZER_PERM_SET_DC_REMOVE      (1ull << 6)
#define SUSCAN_ANALYZER_PERM_SET_IQ_REVERSE     (1ull << 7)
#define SUSCAN_ANALYZER_PERM_SET_AGC            (1ull << 8)
#define SUSCAN_ANALYZER_PERM_OPEN_AUDIO         (1ull << 9)
#define SUSCAN_ANALYZER_PERM_OPEN_RAW           (1ull << 10)
#define SUSCAN_ANALYZER_PERM_OPEN_INSPECTOR     (1ull << 11)
#define SUSCAN_ANALYZER_PERM_SET_FFT_SIZE       (1ull << 12)
#define SUSCAN_ANALYZER_PERM_SET_FFT_FPS        (1ull << 13)
#define SUSCAN_ANALYZER_PERM_SET_FFT_WINDOW     (1ull << 14)
#define SUSCAN_ANALYZER_PERM_SEEK               (1ull << 15)
#define SUSCAN_ANALYZER_PERM_THROTTLE           (1ull << 16)
#define SUSCAN_ANALYZER_PERM_SET_BB_FILTER      (1ull << 17)

#define SUSCAN_ANALYZER_PERM_ALL              0xffffffffffffffffull

#define SUSCAN_ANALYZER_ALL_FILE_PERMISSIONS \
  (SUSCAN_ANALYZER_PERM_ALL &                \
  ~(SUSCAN_ANALYZER_PERM_SET_GAIN       |    \
    SUSCAN_ANALYZER_PERM_SET_ANTENNA    |    \
    SUSCAN_ANALYZER_PERM_SET_BW         |    \
    SUSCAN_ANALYZER_PERM_SET_PPM        |    \
    SUSCAN_ANALYZER_PERM_SET_AGC        |    \
    SUSCAN_ANALYZER_PERM_SET_FREQ))

#define SUSCAN_ANALYZER_ALL_SDR_PERMISSIONS  \
  (SUSCAN_ANALYZER_PERM_ALL &                \
  ~(SUSCAN_ANALYZER_PERM_SEEK |              \
    SUSCAN_ANALYZER_PERM_THROTTLE))

SUSCAN_SERIALIZABLE(suscan_source_gain_info) {
  char *name;
  SUFLOAT min;
  SUFLOAT max;
  SUFLOAT step;
  SUFLOAT value;
};

/*!
 * Constructor for gain info objects.
 * \param value gain value object describing this gain element
 * \return a pointer to the created object or NULL on failure
 * \author Gonzalo José Carracedo Carballal
 */
struct suscan_source_gain_info *suscan_source_gain_info_new(
    const struct suscan_source_gain_value *value);

/*!
 * Constructor for gain info objects (value only).
 * \param name name of the gain element
 * \param value value of this gain in dBs
 * \return a pointer to the created object or NULL on failure
 * \author Gonzalo José Carracedo Carballal
 */
struct suscan_source_gain_info *
suscan_source_gain_info_new_value_only(
    const char *name,
    SUFLOAT value);

/*!
 * Copy-constructor for gain info objects.
 * \param old existing gain info object
 * \return a pointer to the created object or NULL on failure
 * \author Gonzalo José Carracedo Carballal
 */
struct suscan_source_gain_info *
suscan_source_gain_info_dup(
    const struct suscan_source_gain_info *old);

/*!
 * Destructor of the gain info object.
 * \param self pointer to the gain info object
 * \author Gonzalo José Carracedo Carballal
 */
void suscan_source_gain_info_destroy(struct suscan_source_gain_info *self);

SUSCAN_SERIALIZABLE(suscan_source_info) {
  uint64_t permissions;
  uint32_t mtu;

  SUBOOL   realtime;
  SUBOOL   replay;
  SUSCOUNT source_samp_rate;
  SUSCOUNT effective_samp_rate;
  SUFLOAT  measured_samp_rate;

  SUFREQ   frequency;
  SUFREQ   freq_min;
  SUFREQ   freq_max;
  SUFREQ   lnb;

  SUFLOAT  bandwidth;
  SUFLOAT  ppm;
  char    *antenna;
  SUBOOL   dc_remove;
  SUBOOL   iq_reverse;
  SUBOOL   agc;
 
  SUBOOL   have_qth;
  xyz_t    qth;

  struct timeval source_time;

  SUBOOL         seekable;
  struct timeval source_start;
  struct timeval source_end;

  PTR_LIST(struct suscan_source_gain_info, gain);
  PTR_LIST(char, antenna);
};

/*!
 * Initialize a source information structure
 * \param self a pointer to the source info structure
 * \author Gonzalo José Carracedo Carballal
 */
void suscan_source_info_init(struct suscan_source_info *self);

/*!
 * Initialize a source information structure from an existing
 * source information
 * \param self a pointer to the source info structure to be initialized
 * \param origin a pointer to the source info structure to copy
 * \return SU_TRUE on success, SU_FALSE on failure
 * \author Gonzalo José Carracedo Carballal
 */
SUBOOL suscan_source_info_init_copy(
    struct suscan_source_info *self,
    const struct suscan_source_info *origin);

/*!
 * Release allocated resources in the source information structure
 * \param self a pointer to the source info structure
 * \author Gonzalo José Carracedo Carballal
 */
void suscan_source_info_finalize(struct suscan_source_info *self);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _ANALYZER_SOURCE_INFO_H */
