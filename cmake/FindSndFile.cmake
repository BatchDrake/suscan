#
#  CMakeLists.txt: CMake configuration file for sigutils
#
#  Copyright (C) 2019 Gonzalo José Carracedo Carballal
#
#  This program is free software: you can redistribute it and/or modify
#  it under the terms of the GNU Lesser General Public License as
#  published by the Free Software Foundation, either version 3 of the
#  License, or (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful, but
#  WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU Lesser General Public License for more details.
#
#  You should have received a copy of the GNU Lesser General Public
#  License along with this program.  If not, see
#  <http://www.gnu.org/licenses/>
#
#
  
if(PKG_CONFIG_FOUND)
  pkg_check_modules(SNDFILE REQUIRED sndfile>=1.0.2)
elseif(WIN32)
  set(SNDFILE_PATH "$ENV{PROGRAMFILES}/Mega-Nerd/libsndfile" CACHE STRING "Location of libsndfile installation")

  if(NOT DEFINED SNDFILE_PATH OR NOT EXISTS "${SNDFILE_PATH}")
    message(FATAL_ERROR "libsndfile path not found. Please specify the installation path of Win32's libsndfile using the SNDFILE_PATH variable. You can download all required development files from http://www.mega-nerd.com/libsndfile/#Download")
  endif()
    
  if(NOT EXISTS "${SNDFILE_PATH}/include/sndfile.h")
    message(FATAL_ERROR "libsndfile path ${SNDFILE_PATH} does not contain sndfile.h")
  endif()
  
  if(NOT EXISTS "${SNDFILE_PATH}/bin/libsndfile-1.dll")
    message(FATAL_ERROR "libsndfile path ${SNDFILE_PATH} does not contain libsndfile-1.dll")
  endif()
  
  set(SNDFILE_INCLUDE_DIRS "${SNDFILE_PATH}/include")
  set(SNDFILE_LIBRARIES "${SNDFILE_PATH}/bin/libsndfile-1.dll")
else()
  message(FATAL_ERROR "Unsupported platform")
endif()