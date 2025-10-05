# 
# Copyright (c) 2025 Gonzalo José Carracedo Carballal
# 
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
# 
#     http://www.apache.org/licenses/LICENSE-2.0
# 
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# 

cmake_minimum_required(VERSION 3.14)

project(plugin_template LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FindPkgConfig)

# Default Suscan plugin directory
file(
  REAL_PATH
  "~/.suscan/plugins"
  SUSCAN_DEFAULT_PLUGIN_PATH
  EXPAND_TILDE)

#
# Configurable plugin directory. The user can adjust this by setting
# -DPLUGIN_DIRECTORY when calling cmake. Default: ~/.suscan/plugins
#
set(
  PLUGIN_DIRECTORY
  "${SUSCAN_DEFAULT_PLUGIN_PATH}" 
  CACHE STRING
  "Suscan plugin installation directory")

#
# Plugin dependencies. At least the following 4 are needed:
#  - sigutils
#  - suscan
#  - fftw3f
#  - sndfile
#

pkg_check_modules(SIGUTILS  REQUIRED sigutils>=0.3)
pkg_check_modules(SUSCAN    REQUIRED suscan>=0.3)
pkg_check_modules(SNDFILE   REQUIRED sndfile>=1.0.2)
pkg_check_modules(FFTW3     REQUIRED fftw3f>=3.0)

#
# Plugin source code. Ideally, keep file names sorted in dictionary order.
#
set(
  PLUGIN_SOURCES
  src/registration.c)

set(
  PLUGIN_HEADERS
  include/plugin.h)

add_library(example_plugin SHARED ${PLUGIN_SOURCES} ${PLUGIN_HEADERS})

install(
  TARGETS example_plugin
  DESTINATION ${PLUGIN_DIRECTORY})

#
# Plugin header dependencies. Again, one for each dependency. At least the
# 4 ones mentioned above.
#
target_include_directories(
  example_plugin 
  INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  SYSTEM PUBLIC
  ${SNDFILE_INCLUDE_DIRS}
  ${SIGUTILS_INCLUDE_DIRS}
  ${SUSCAN_INCLUDE_DIRS}
  ${FFTW3_INCLUDE_DIRS})

#
# CFLAGS are important for both Suscan and Sigutils, as they control the size of
# the default sample type.
#

target_compile_options(
  example_plugin 
  PRIVATE ${SIGUTILS_CFLAGS} ${SUSCAN_CFLAGS})

#
# If your plugin needs 3rd party libraries, add them here
#  
# target_link_libraries(
#  example_plugin
#  PRIVATE ${MY_WHATEVER_LIBRARIES})
#

