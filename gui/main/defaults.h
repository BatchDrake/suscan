/*

  Copyright (C) 2017 Gonzalo José Carracedo Carballal

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

#ifndef _GUI_DEFAULTS_H
#define _GUI_DEFAULTS_H

/* Panadapter colors */
#define SUSCAN_DEFAULT_PA_BG_COLOR     "#000000"
#define SUSCAN_DEFAULT_PA_FG_COLOR     "#ffff00"
#define SUSCAN_DEFAULT_PA_AXES_COLOR   "#808080"
#define SUSCAN_DEFAULT_PA_TEXT_COLOR   "#ffffff"

/* Inspector colors */
#define SUSCAN_DEFAULT_INSP_BG_COLOR   "#000000"
#define SUSCAN_DEFAULT_INSP_FG_COLOR   "#ffff00"
#define SUSCAN_DEFAULT_INSP_AXES_COLOR "#808080"
#define SUSCAN_DEFAULT_INSP_TEXT_COLOR "#ffffff"

/* LCD colors */
#define SUSCAN_DEFAULT_LCD_BG_COLOR    "#90b156"
#define SUSCAN_DEFAULT_LCD_FG_COLOR    "#000000"

/* Analyzer settings */
#define SUSCAN_DEFAULT_SPECTRUM_AVG_FACTOR 0.01
#define SUSCAN_DEFAULT_SIGNAL_AVG_FACTOR   0.001
#define SUSCAN_DEFAULT_NOISE_AVG_FACTOR    0.5
#define SUSCAN_DEFAULT_SNR_THRESHOLD       6
#define SUSCAN_DEFAULT_BUFFER_SIZE         1024
#define SUSCAN_DEFAULT_WINDOW_FUNC         "blackmann-harris"
#define SUSCAN_DEFAULT_CHANNEL_INTERVAL    0.1
#define SUSCAN_DEFAULT_PSD_INTERVAL        0.04 /* 25 fps looks good */

#endif /* _GUI_DEFAULTS_H */
