# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
CodeAnalysis
-------

Add code format target.

Custom Targets
^^^^^^^^^^^^^^^^

This module provides the following custom target, if found:

``cppcheck-analysis``
  The code analysis custom target.

#]=======================================================================]

if(TARGET codeanalysis)
    return()
endif()

find_package(CppCheck)

if(CppCheck_FOUND)
  message("Cppcheck analysis already added")
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/analysis/cppcheck)
    add_custom_target(codeanalysis
        COMMAND ${CPPCHECK_COMMAND})
endif()
