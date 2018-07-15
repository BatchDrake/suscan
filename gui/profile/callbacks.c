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

#define SU_LOG_DOMAIN "gui-profile"

#include "profile.h"

void
suscan_gui_profile_on_changed(GtkWidget *widget, gpointer data)
{
  suscan_gui_profile_t *profile = (suscan_gui_profile_t *) data;

  if (!profile->in_callback) {
    profile->in_callback = SU_TRUE;
    profile->changed = SU_TRUE;

    suscan_gui_profile_update_sensitivity(profile);

    /* Channel or device changed. Update antennas */
    if (widget == GTK_WIDGET(profile->channelSpinButton) ||
        widget == GTK_WIDGET(profile->deviceComboBoxText)) {
      (void) suscan_gui_profile_update_device(profile);
      (void) suscan_gui_profile_update_gains(profile);
      suscan_gui_profile_update_antennas(profile);
    }

    profile->in_callback = SU_FALSE;
  }

}
