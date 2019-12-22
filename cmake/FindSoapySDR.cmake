#
#  FindSoapySDR.cmake: Find SoapySDR .cmake file
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
  pkg_check_modules(SOAPYSDR REQUIRED SoapySDR>=0.5.0)
elseif(WIN32)
  set(SOAPYSDR_PATH "$ENV{PROGRAMFILES}/SoapySDR" CACHE STRING "Location of SoapySDR installation")

  if(NOT DEFINED SOAPYSDR_PATH OR NOT EXISTS "${SOAPYSDR_PATH}")
    message(FATAL_ERROR "SoapySDR installation not found (or SOAPYSDR_PATH not properly defined). Please download the latest SoapySDR and follow the build instructions in https://github.com/pothosware/SoapySDR/wiki/BuildGuide#id3")
  endif()
    
  if(NOT EXISTS "${SOAPYSDR_PATH}/cmake/SoapySDRConfig.cmake")
    message(FATAL_ERROR "SoapySDR path ${SOAPYSDR_PATH} does not contain cmake/SoapySDRConfig.cmake")
  endif()
  
  include(${SOAPYSDR_PATH}/cmake/SoapySDRConfig.cmake)
  set(SOAPYSDR_INCLUDE_DIRS "${SoapySDR_INCLUDE_DIRS}")
  set(SOAPYSDR_LIBRARIES "${SoapySDR_LIBRARIES}")
else()
  message(FATAL_ERROR "Unsupported platform")
endif()

