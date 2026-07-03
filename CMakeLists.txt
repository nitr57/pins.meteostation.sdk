# ###############################################################################
# MIT License
#
# Copyright (c) 2026 Nico Trost
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
# ###############################################################################

cmake_minimum_required(VERSION 3.22)
project(MeteoStationSDK VERSION 1.2.0 DESCRIPTION "MeteoStation SDK")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find libudev package
if(UNIX)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBUDEV libudev)
endif()

# SDK Library
add_library(MeteoStationSDK SHARED
    MeteoStationSDK.cpp
    MeteoStationLogging.cpp
    MeteoStationSerialPort.cpp
    MeteoStationDevice.cpp
    MeteoStationProtocol.cpp)

# Public headers
target_include_directories(MeteoStationSDK PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Link libudev
if(UNIX)
    if(LIBUDEV_FOUND)
        target_include_directories(MeteoStationSDK PRIVATE ${LIBUDEV_INCLUDE_DIRS})
        target_link_libraries(MeteoStationSDK PRIVATE ${LIBUDEV_LIBRARIES})
    else()
        message(FATAL_ERROR "libudev not found. Install it with: sudo apt-get install libudev-dev")
    endif()
endif()

if(WIN32)
    target_link_libraries(MeteoStationSDK PRIVATE SetupAPI)
    # Prevent Windows headers from defining min/max macros
    target_compile_definitions(MeteoStationSDK PRIVATE NOMINMAX)
endif()

# Test executables
add_executable(test_meteo_station test_meteo_station.cpp)
target_link_libraries(test_meteo_station MeteoStationSDK)

# Installation
install(TARGETS MeteoStationSDK
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)
install(FILES MeteoStationSDK.h MeteoStationLogging.h MeteoStationSerialPort.h MeteoStationDevice.h MeteoStationProtocol.h DESTINATION include)

# CPack configuration for Debian package generation
if(UNIX)
    set(CPACK_GENERATOR "DEB")
    set(CPACK_PACKAGE_CONTACT "Nico")
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/nitr57/MeteoStationSDK")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Nico")
    set(CPACK_DEBIAN_PACKAGE_DESCRIPTION_SUMMARY "SDK for controlling Meteo Station devices via serial communication")

    # Runtime dependencies
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libudev1")

    # License
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")

    include(CPack)
endif()
