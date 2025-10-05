/*
 * Copyright (c) 2025 Gonzalo José Carracedo Carballal
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *     http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define SU_LOG_DOMAIN "example_plugin"

#include <sigutils/log.h>
#include <suscan/suscan.h>

SUSCAN_PLUGIN("example", "Example plugin"); /* Plugin name and description */
SUSCAN_PLUGIN_VERSION(0, 1, 0);             /* Plugin version */
SUSCAN_PLUGIN_API_VERSION(0, 3, 0);         /* Expected Suscan API version */
/* SUSCAN_PLUGIN_DEPENDS("another_plugin"); */

SUSCAN_PLUGIN_ENTRY(plugin)
{
  /* Add here all the registration calls you need */

  SU_INFO("Plugin entry called!\n");

  return SU_TRUE; /* Return SU_FALSE if something went wrong. */
}
