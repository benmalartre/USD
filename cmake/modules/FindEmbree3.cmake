#
# Copyright 2017 Pixar
#
# Licensed under the Apache License, Version 2.0 (the "Apache License")
# with the following modification; you may not use this file except in
# compliance with the Apache License and the following modification to it:
# Section 6. Trademarks. is deleted and replaced with:
#
# 6. Trademarks. This License does not grant permission to use the trade
#    names, trademarks, service marks, or product names of the Licensor
#    and its affiliates, except as required to comply with Section 4(c) of
#    the License and to reproduce the content of the NOTICE file.
#
# You may obtain a copy of the Apache License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the Apache License with the above modification is
# distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied. See the Apache License for the specific
# language governing permissions and limitations under the Apache License.
#
#=============================================================================
#
# The module defines the following variables:
#   EMBREE3_INCLUDE_DIR - path to embree3 header directory
#   EMBREE3_LIBRARY     - path to embree3 library file
#       EMBREE3_FOUND   - true if embree3 was found
#
# Example usage:
#   find_package(EMBREE3)
#   if(EMBREE3_FOUND)
#     message("EMBREE3 found: ${EMBREE3_LIBRARY}")
#   endif()
#
#=============================================================================

if (APPLE)
    set (EMBREE3_LIB_NAME libembree.dylib)
elseif (UNIX)
    set (EMBREE3_LIB_NAME libembree.so)
elseif (WIN32)
    set (EMBREE3_LIB_NAME embree.lib)
endif()

find_library(EMBREE3_LIBRARY
        "${EMBREE3_LIB_NAME}"
    HINTS
        "${EMBREE3_LOCATION}/lib64"
        "${EMBREE3_LOCATION}/lib"
        "$ENV{EMBREE3_LOCATION}/lib64"
        "$ENV{EMBREE3_LOCATION}/lib"
    DOC
        "Embree3 library path"
)

find_path(EMBREE3_INCLUDE_DIR
    embree3/rtcore.h
HINTS
    "${EMBREE3_LOCATION}/include"
    "$ENV{EMBREE3_LOCATION}/include"
DOC
    "Embree3 headers path"
)

if (EMBREE3_INCLUDE_DIR AND EXISTS "${EMBREE_INCLUDE_DIR}/embree3/rtcore.h" )
    set(EMBREE3_FOUND 1)
endif()

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(EMBREE3
    REQUIRED_VARS
        EMBREE3_INCLUDE_DIR
        EMBREE3_LIBRARY
)
