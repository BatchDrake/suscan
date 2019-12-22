#
#  FindSigutils.cmake: Find sigutils installation
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
  pkg_check_modules(SIGUTILS REQUIRED sigutils>=0.1)
elseif(WIN32)
  set(SIGUTILS_PATH "$ENV{PROGRAMFILES}/sigutils" CACHE STRING "Location of Sigutils installation")

  if(NOT DEFINED SIGUTILS_PATH OR NOT EXISTS "${SIGUTILS_PATH}")
    message(FATAL_ERROR "Sigutils path not found. Please specify the installation path of Win32's Sigutils using the SIGUTILS_PATH variable. You can download the latest Sigutils Windows release from http://actinid.org")
  endif()
    
  if(NOT EXISTS "${SIGUTILS_PATH}/include/sigutils/sigutils.h")
    message(FATAL_ERROR "Sigutils path ${SIGUTILS_PATH} does not contain the include file sigutils.h")
  endif()
  
  if(NOT EXISTS "${SIGUTILS_PATH}/lib/libsigutils.dll")
    message(FATAL_ERROR "Sigutils path ${SIGUTILS_PATH} does not contain libsigutils.dll")
  endif()
  
  set(SIGUTILS_INCLUDE_DIRS "${SIGUTILS_PATH}/include")
  set(SIGUTILS_LIBRARIES "${SIGUTILS_PATH}/lib/libsigutils.dll")
else()
  message(FATAL_ERROR "Unsupported platform")
endif()
