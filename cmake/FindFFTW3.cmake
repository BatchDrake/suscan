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
  pkg_check_modules(FFTW3 REQUIRED fftw3f>=3.0)
elseif(WIN32)
  if(NOT DEFINED FFTW3_PATH)
    message(FATAL_ERROR "FFTW3 path not found. Please specify the installation path of Win32 FFTW3 lib using the FFTW3_PATH variable. You can download all the required development files from http://www.fftw.org/install/windows.html")
  endif()
  
  if(NOT EXISTS "${FFTW3_PATH}")
    message(FATAL_ERROR "Specified FFTW3 directory ${FFTW3_PATH} not found")
  endif()
  
  if(NOT EXISTS "${FFTW3_PATH}/fftw3.h")
    message(FATAL_ERROR "FFTW path ${FFTW3_PATH} does not contain fftw3.h")
  endif()
  
  if(NOT EXISTS "${FFTW3_PATH}/libfftw3f-3.dll")
    message(FATAL_ERROR "FFTW path ${FFTW3_PATH} does not contain libfftw3f-3.dll")
  endif()
  
  set(FFTW3_INCLUDE_DIRS "${FFTW3_PATH}")
  set(FFTW3_LIBRARIES "${FFTW3_PATH}/libfftw3f-3.dll")
else()
  message(FATAL_ERROR "Unsupported platform")
endif()