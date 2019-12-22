#
#  FindLibxml2.cmake: Find libxml2 .cmake file
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
  pkg_check_modules(LIBXML2 REQUIRED libxml-2.0>=2.9.0)
elseif(WIN32)
  if(NOT DEFINED LIBXML2_PATH OR NOT EXISTS "${LIBXML2_PATH}")
    message(FATAL_ERROR "libxml-2.0 installation not found (or LIBXML2_PATH not properly defined). Please download the latest release of libxml-2.0 development files from http://xmlsoft.org/downloads.html and update LIBXML2_PATH accordingly.")
  endif()
    
  if(NOT EXISTS "${LIBXML2_PATH}/lib/cmake/libxml2/libxml2-config.cmake")
    message(FATAL_ERROR "libxml2 path ${LIBXML2_PATH} does not contain lib/cmake/libxml2/libxml2-config.cmake")
  endif()
  
  set(ZLIB_LIBRARY "${LIBXML2_PATH}/lib/libz.a")
  set(ZLIB_INCLUDE_DIR "${LIBXML2_PATH}/include")
  include(${LIBXML2_PATH}/lib/cmake/libxml2/libxml2-config.cmake)
else()
  message(FATAL_ERROR "Unsupported platform")
endif()
