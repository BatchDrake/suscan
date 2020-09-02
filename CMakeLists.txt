#
#  CMakeLists.txt: CMake configuration file for suscan
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
  
cmake_minimum_required(VERSION 3.5.1)

set(SUSCAN_VERSION_MAJOR 0)
set(SUSCAN_VERSION_MINOR 2)
set(SUSCAN_VERSION_PATCH 0)

set(SUSCAN_ABI_VERSION   1)

set(
  SUSCAN_VERSION
  ${SUSCAN_VERSION_MAJOR}.${SUSCAN_VERSION_MINOR}.${SUSCAN_VERSION_PATCH})

project(
  suscan
  VERSION ${SUSCAN_VERSION}
  LANGUAGES C)

include(FindPkgConfig)

# Make sure CMAKE_INSTALL_LIBDIR is defined for all systems
if(NOT DEFINED CMAKE_INSTALL_LIBDIR)
  set(CMAKE_INSTALL_LIBDIR lib)
endif()

# Find requirements
find_package(Threads)
pkg_check_modules(SIGUTILS REQUIRED sigutils>=0.1)
pkg_check_modules(SNDFILE  REQUIRED sndfile>=1.0.2)
pkg_check_modules(FFTW3    REQUIRED fftw3f>=3.0)
pkg_check_modules(SOAPYSDR REQUIRED SoapySDR>=0.5.0)
pkg_check_modules(XML2     REQUIRED libxml-2.0>=2.9.0)
pkg_check_modules(VOLK              volk>=1.0)
pkg_check_modules(PORTAUDIO         portaudio-2.0>=19)

# Source location
set(SRCDIR       src)
set(CODECLIBDIR  codec)
set(CODECDIR     ${CODECLIBDIR}/codecs)
set(ANALYZERDIR  analyzer)
set(ESTIMATORDIR ${ANALYZERDIR}/estimators)
set(INSPECTORDIR ${ANALYZERDIR}/inspector/impl)
set(SPECTSRCDIR  ${ANALYZERDIR}/spectsrcs)
set(UTILDIR      util)
set(RSRCDIR      rsrc)
set(CLIDIR       cli)
set(VERSIONDIR   analyzer)

# Compiler flags
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING
       "Choose the type of build, options are: None Debug Release RelWithDebInfo
MinSizeRel."
       FORCE )
endif()

set(SUSCAN_PKGDIR "${CMAKE_INSTALL_PREFIX}" CACHE STRING
       "Set package directory (where initial config and data files are read from)"
       FORCE )
       
string(REPLACE ";" " " SIGUTILS_SPC_CFLAGS "${SIGUTILS_CFLAGS}")
string(REPLACE ";" " " SIGUTILS_SPC_LDFLAGS "${SIGUTILS_LDFLAGS}")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall ${SIGUTILS_CONFIG_CFLAGS}")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DPKGDATADIR='\"${SUSCAN_PKGDIR}/share/suscan\"'")
# The following hack exposes __FILENAME__ to source files as the relative
# path of each source file.
set(CMAKE_C_FLAGS
"${CMAKE_C_FLAGS} -D__FILENAME__='\"$(subst ${CMAKE_SOURCE_DIR}/,,$(abspath $<))\"'")

#
# If you are building suscan for your own software distribution, you may want
# to set PKGVERSION to some descriptive string.
#

if (DEFINED PKGVERSION)
  set(
    CMAKE_C_FLAGS
    "${CMAKE_C_FLAGS} -DSUSCAN_PKGVERSION='\"${PKGVERSION}\"'")
endif()

set(CMAKE_C_FLAGS_DEBUG   "${CMAKE_C_FLAGS_DEBUG} -O0 -ggdb")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3 -ffast-math -DNDEBUG")

set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -s")
set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} -s")

# Fix for MacOS X broken libxml2 Homebrew installation in MacOS Catalina
if(APPLE)
  set(XML2_INCLUDE_DIRS "/usr/local/opt/libxml2/include/libxml2")
endif()

########################## pkg-config description #############################
set(SIGUTILS_PC_FILE_PATH "${PROJECT_BINARY_DIR}/suscan.pc")
  
set(
  INSTALL_PKGCONFIG_DIR 
  "${CMAKE_INSTALL_LIBDIR}/pkgconfig"
  CACHE PATH "Installation directory for pkgconfig (.pc) files")
set(SU_PC_CFLAGS "${SIGUTILS_CONFIG_CFLAGS}")
set(SU_PC_LIBRARIES "-l${SNDFILE_LIBRARIES} -lm -l${FFTW3_LIBRARIES} -l${VOLK_LIBRARIES}")
configure_file(suscan.pc.in "${SIGUTILS_PC_FILE_PATH}" @ONLY)

install(
  FILES "${SIGUTILS_PC_FILE_PATH}"
  DESTINATION "${INSTALL_PKGCONFIG_DIR}")
  
############################ Suscan library build #############################
set(RSRC_CONFIG_FILES
  ${RSRCDIR}/autogains.xml
  ${RSRCDIR}/palettes.xml
  ${RSRCDIR}/frequency_allocations.xml)
  
set(UTIL_HEADERS
  ${UTILDIR}/compat.h
  ${UTILDIR}/hashlist.h
  ${UTILDIR}/macos-barriers.h
  ${UTILDIR}/macos-barriers.imp.h
  ${UTILDIR}/confdb.h
  ${UTILDIR}/cfg.h
  ${UTILDIR}/object.h 
  ${UTILDIR}/rbtree.h)
  
set(UTIL_SOURCES
  ${UTILDIR}/cfg.c
  ${UTILDIR}/compat.c
  ${UTILDIR}/confdb.c
  ${UTILDIR}/deserialize.c
  ${UTILDIR}/hashlist.c
  ${UTILDIR}/object.c
  ${UTILDIR}/rbtree.c
  ${UTILDIR}/serialize.c)

set(VERSION_HEADERS ${VERSIONDIR}/version.h)

set(VERSION_SOURCES ${VERSIONDIR}/version.c)

set(INSPECTOR_LIB_HEADERS
  ${ANALYZERDIR}/inspector/inspector.h
  ${ANALYZERDIR}/inspector/params.h
  ${ANALYZERDIR}/inspector/interface.h)

set(INSPECTOR_LIB_SOURCES
  ${ANALYZERDIR}/inspector/inspector.c
  ${ANALYZERDIR}/inspector/interface.c
  ${ANALYZERDIR}/inspector/params.c
  ${INSPECTORDIR}/ask.c
  ${INSPECTORDIR}/audio.c
  ${INSPECTORDIR}/fsk.c
  ${INSPECTORDIR}/psk.c
  ${INSPECTORDIR}/raw.c)

set(CODEC_LIB_HEADERS ${CODECLIBDIR}/codec.h)

set(CODEC_LIB_SOURCES 
  ${CODECLIBDIR}/codec.c
  ${CODECDIR}/diff.c)

set(ANALYZER_LIB_HEADERS
  ${ANALYZERDIR}/realtime.h
  ${ANALYZERDIR}/msg.h
  ${ANALYZERDIR}/inspsched.h
  ${ANALYZERDIR}/spectsrc.h
  ${ANALYZERDIR}/worker.h
  ${ANALYZERDIR}/estimator.h
  ${ANALYZERDIR}/source.h
  ${ANALYZERDIR}/symbuf.h
  ${ANALYZERDIR}/mq.h
  ${ANALYZERDIR}/throttle.h
  ${ANALYZERDIR}/analyzer.h)

set(ESTIMATOR_SOURCES
  ${ESTIMATORDIR}/fac.c
  ${ESTIMATORDIR}/nonlinear.c)

set(SPECTSRC_SOURCES
  ${SPECTSRCDIR}/cyclo.c
  ${SPECTSRCDIR}/fmcyclo.c
  ${SPECTSRCDIR}/fmspect.c
  ${SPECTSRCDIR}/pmspect.c
  ${SPECTSRCDIR}/timediff.c
  ${SPECTSRCDIR}/exp-2.c
  ${SPECTSRCDIR}/exp-4.c
  ${SPECTSRCDIR}/exp-8.c
  ${SPECTSRCDIR}/psd.c)
  
set(ANALYZER_LIB_SOURCES
  ${ANALYZERDIR}/workers/channel.c
  ${ANALYZERDIR}/workers/wide.c
  ${ANALYZERDIR}/analyzer.c
  ${ANALYZERDIR}/bufpool.c
  ${ANALYZERDIR}/client.c
  ${ANALYZERDIR}/estimator.c
  ${ANALYZERDIR}/inspsched.c
  ${ANALYZERDIR}/insp-server.c
  ${ANALYZERDIR}/mq.c
  ${ANALYZERDIR}/msg.c
  ${ANALYZERDIR}/slow.c
  ${ANALYZERDIR}/source.c
  ${ANALYZERDIR}/spectsrc.c
  ${ANALYZERDIR}/symbuf.c
  ${ANALYZERDIR}/throttle.c
  ${ANALYZERDIR}/worker.c
  ${ESTIMATOR_SOURCES}
  ${SPECTSRC_SOURCES})

link_directories(
  ${PROJECT_BINARY_DIR}
  ${SNDFILE_LIBRARY_DIRS}
  ${FFTW3_LIBRARY_DIRS}
  ${SOAPYSDR_LIBRARY_DIRS}
  ${SIGUTILS_LIBRARY_DIRS}
  ${XML2_LIBRARY_DIRS})
 
add_library(
  suscan SHARED
  ${UTIL_HEADERS}
  ${UTIL_SOURCES}
  ${INSPECTOR_LIB_HEADERS}
  ${INSPECTOR_LIB_SOURCES}
  ${CODEC_LIB_HEADERS}
  ${CODEC_LIB_SOURCES}
  ${ANALYZER_LIB_HEADERS}
  ${ANALYZER_LIB_SOURCES}
  ${VERSION_SOURCES})
  
# Private header directories
target_include_directories(
  suscan 
  PRIVATE . ${UTILDIR} ${ANALYZERDIR} ${ANALYZERDIR}/inspector ${CODECLIBDIR})

set_property(TARGET suscan PROPERTY VERSION   ${SUSCAN_VERSION})
set_property(TARGET suscan PROPERTY SOVERSION ${SUSCAN_ABI})

set_property(TARGET suscan PROPERTY COMPILE_FLAGS "${SIGUTILS_SPC_CFLAGS}")
set_property(TARGET suscan PROPERTY LINK_FLAGS    "${SIGUTILS_SPC_LDFLAGS}")

# Required dependencies
if(APPLE)
  # Required to retrieve bundle path
  target_link_libraries(suscan "-framework CoreFoundation")
endif()

target_link_libraries(suscan m ${SIGUTILS_LIBRARIES})

target_include_directories(suscan SYSTEM PUBLIC ${SNDFILE_INCLUDE_DIRS})
target_link_libraries(suscan ${SNDFILE_LIBRARIES})

target_include_directories(suscan SYSTEM PUBLIC ${SNDFILE_INCLUDE_DIRS})
target_link_libraries(suscan ${SNDFILE_LIBRARIES})

target_include_directories(suscan SYSTEM PUBLIC ${FFTW3_INCLUDE_DIRS})
target_link_libraries(suscan ${FFTW3_LIBRARIES})

target_include_directories(suscan SYSTEM PUBLIC ${SOAPYSDR_INCLUDE_DIRS})
target_link_libraries(suscan ${SOAPYSDR_LIBRARIES})

target_include_directories(suscan SYSTEM PUBLIC ${XML2_INCLUDE_DIRS})
target_link_libraries(suscan ${XML2_LIBRARIES})

target_link_libraries(suscan ${CMAKE_THREAD_LIBS_INIT})

# Optional dependencies
if(VOLK_FOUND)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DHAVE_VOLK=1")
  target_include_directories(suscan SYSTEM PUBLIC ${VOLK_INCLUDE_DIRS})
  link_directories(${VOLK_LIBRARY_DIRS})
endif()

install(
  FILES ${ANALYZER_LIB_HEADERS} 
  DESTINATION include/suscan/analyzer)

install(
  FILES ${INSPECTOR_LIB_HEADERS} 
  DESTINATION include/suscan/analyzer/inspector)

install(
  FILES ${CODEC_LIB_HEADERS} 
  DESTINATION include/suscan/codec)

install(
  FILES ${UTIL_HEADERS} 
  DESTINATION include/suscan/util)

install(
  FILES ${VERSION_HEADERS}
  DESTINATION include/suscan/analyzer)

install(
  FILES ${RSRC_CONFIG_FILES}
  DESTINATION share/suscan/config)
   
install(TARGETS suscan DESTINATION ${CMAKE_INSTALL_LIBDIR})

########################### Suscan test executable ############################
set(SUSCAN_HEADERS ${SRCDIR}/suscan.h)
    
set(SUSCAN_SOURCES 
  ${SRCDIR}/common.c 
  ${SRCDIR}/fingerprint.c 
  ${SRCDIR}/lib.c
  ${SRCDIR}/main.c)
  
add_executable(
  suscan.status
  ${SUSCAN_HEADERS} 
  ${SUSCAN_SOURCES})

# Private header directories
target_include_directories(
  suscan.status 
  PRIVATE . ${UTILDIR} ${CODECLIB_DIR} ${SRCDIR})

# Required dependencies
set_target_properties(suscan.status PROPERTIES COMPILE_FLAGS "${SIGUTILS_SPC_CFLAGS}")
set_target_properties(suscan.status PROPERTIES LINK_FLAGS "${SIGUTILS_SPC_LDFLAGS}")

link_directories(${PROJECT_BINARY_DIR} ${SOAPYSDR_LIBRARY_DIRS})

target_link_libraries(suscan.status sigutils)
target_link_libraries(suscan.status suscan)
target_link_libraries(suscan.status m)

target_include_directories(suscan.status SYSTEM PUBLIC ${SNDFILE_INCLUDE_DIRS})
target_link_libraries(suscan.status ${SNDFILE_LIBRARIES})

target_include_directories(suscan.status SYSTEM PUBLIC ${FFTW3_INCLUDE_DIRS})
target_link_libraries(suscan.status ${FFTW3_LIBRARIES})

target_include_directories(suscan.status SYSTEM PUBLIC ${SOAPYSDR_INCLUDE_DIRS})
target_link_libraries(suscan.status ${SOAPYSDR_LIBRARIES})

target_include_directories(suscan.status SYSTEM PUBLIC ${XML2_INCLUDE_DIRS})
target_link_libraries(suscan.status ${XML2_LIBRARIES})

target_link_libraries(suscan.status ${CMAKE_THREAD_LIBS_INIT})

# Optional dependencies
if(VOLK_FOUND)
  target_include_directories(suscan.status SYSTEM PUBLIC ${VOLK_INCLUDE_DIRS})
  target_link_libraries(suscan.status ${VOLK_LIBRARIES})
endif()

install(TARGETS suscan.status DESTINATION bin)

######################### Suscan Command Line tool ############################
set(SUSCLI_HEADERS
  ${CLIDIR}/audio.h
  ${CLIDIR}/chanloop.h
  ${CLIDIR}/cli.h
  ${CLIDIR}/cmds.h
  ${CLIDIR}/datasaver.h
  ${SRCDIR}/suscan.h)

set(SUSCLI_SOURCES
  ${CLIDIR}/audio.c
  ${CLIDIR}/chanloop.c
  ${CLIDIR}/cli.c
  ${CLIDIR}/cmd/profiles.c
  ${CLIDIR}/cmd/rms.c
  ${CLIDIR}/cmd/profinfo.c
  ${CLIDIR}/datasavers/mat5.c
  ${CLIDIR}/datasavers/matlab.c
  ${CLIDIR}/datasavers/tcp.c
  ${CLIDIR}/datasaver.c
  ${SRCDIR}/lib.c
  ${SRCDIR}/suscli.c)

add_executable(
  suscli
  ${SUSCLI_HEADERS} 
  ${SUSCLI_SOURCES})

# Private header directories
target_include_directories(
  suscli
  PRIVATE . ${UTILDIR} ${CODECLIB_DIR} ${SRCDIR} ${CLIDIR})

if(PORTAUDIO_FOUND)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DHAVE_PORTAUDIO=1")
  target_include_directories(suscli SYSTEM PUBLIC ${PORTAUDIO_INCLUDE_DIRS})
  string(REPLACE ";" " " PORTAUDIO_SPC_LDFLAGS "${PORTAUDIO_LDFLAGS}")
  set(SIGUTILS_SPC_LDFLAGS "${SIGUTILS_SPC_LDFLAGS} ${PORTAUDIO_SPC_LDFLAGS}")
endif()

# Required dependencies
set_target_properties(suscli PROPERTIES COMPILE_FLAGS "${SIGUTILS_SPC_CFLAGS}")
set_target_properties(suscli PROPERTIES LINK_FLAGS "${SIGUTILS_SPC_LDFLAGS}")

link_directories(${PROJECT_BINARY_DIR} ${SOAPYSDR_LIBRARY_DIRS})

target_link_libraries(suscli sigutils)
target_link_libraries(suscli suscan)
target_link_libraries(suscli m)

target_include_directories(suscli SYSTEM PUBLIC ${SNDFILE_INCLUDE_DIRS})
target_link_libraries(suscli ${SNDFILE_LIBRARIES})

target_include_directories(suscli SYSTEM PUBLIC ${FFTW3_INCLUDE_DIRS})
target_link_libraries(suscli ${FFTW3_LIBRARIES})

target_include_directories(suscli SYSTEM PUBLIC ${SOAPYSDR_INCLUDE_DIRS})
target_link_libraries(suscli ${SOAPYSDR_LIBRARIES})

target_include_directories(suscli SYSTEM PUBLIC ${XML2_INCLUDE_DIRS})
target_link_libraries(suscli ${XML2_LIBRARIES})

target_link_libraries(suscli ${CMAKE_THREAD_LIBS_INIT})

# Optional dependencies
if(VOLK_FOUND)
  target_include_directories(suscli SYSTEM PUBLIC ${VOLK_INCLUDE_DIRS})
  target_link_libraries(suscli ${VOLK_LIBRARIES})
endif()

install(TARGETS suscli DESTINATION bin)
