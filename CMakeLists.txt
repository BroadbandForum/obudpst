# Copyright (c) 2020, Broadband Forum
# Copyright (c) 2020, Axiros GmbH
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
#
# 3. Neither the name of the copyright holder nor the names of its
#    contributors may be used to endorse or promote products derived from this
#    software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
project(OB-UDPST)
cmake_minimum_required(VERSION 3.0)
if(${CMAKE_VERSION} VERSION_GREATER "3.3.0")
        cmake_policy(SET CMP0057 NEW)
endif()
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(SOFTWARE_VER "7.4.0")

INCLUDE (CheckIncludeFiles)
INCLUDE (CheckFunctionExists)
INCLUDE (CheckSymbolExists)
INCLUDE (FindPackageHandleStandardArgs)

find_package(PkgConfig)

find_package(OpenSSL)
if(OPENSSL_FOUND)
        set(HAVE_OPENSSL TRUE)
        link_directories(${OPENSSL_LIBRARY_DIRS})
        include_directories(${OPENSSL_INCLUDE_DIR})
        add_definitions(-DAUTH_KEY_ENABLE)
        set(libraries ${libraries} ${OPENSSL_LIBRARIES})
endif()

include_directories(${PROJECT_SOURCE_DIR})
include_directories(${PROJECT_BINARY_DIR})
include_directories(${CMAKE_BINARY_DIR})

CHECK_INCLUDE_FILES (malloc.h HAVE_MALLOC_H)
CHECK_INCLUDE_FILES (stdlib.h HAVE_STDLIB_H)
CHECK_INCLUDE_FILES (string.h HAVE_STRING_H)
CHECK_INCLUDE_FILES (unistd.h HAVE_UNISTD_H)
CHECK_INCLUDE_FILES (memory.h HAVE_MEMORY_H)
CHECK_INCLUDE_FILES (sys/sysinfo.h HAVE_SYSINFO_H)
CHECK_INCLUDE_FILES (sys/sysctl.h HAVE_SYSCTL_H)
CHECK_INCLUDE_FILES (linux/socket.h HAVE_SIOCGIFHWADDR)
CHECK_SYMBOL_EXISTS (LLADDR "sys/socket.h;net/if_dl.h" HAVE_NET_IF_DL_H)

CHECK_FUNCTION_EXISTS (sendmmsg HAVE_SENDMMSG)
CHECK_FUNCTION_EXISTS (timegm HAVE_TIMEGM)
CHECK_FUNCTION_EXISTS (mkfifo HAVE_MKFIFO)
CHECK_FUNCTION_EXISTS (getifaddrs HAVE_GETIFADDRS)
CHECK_FUNCTION_EXISTS (fork HAVE_WORKING_FORK)

OPTION(DISABLE_INT_TIMER "Disable the timer for systems which don't provide the required timer resolution. Increases CPU utilization." OFF)

add_definitions(-DSYSCONFDIR=\"${CMAKE_INSTALL_PREFIX}/etc\")
add_definitions(-DLOCALSTATEDIR=\"${CMAKE_INSTALL_PREFIX}/var/lib\")
add_definitions(-DLOGDIR=\"${CMAKE_INSTALL_PREFIX}/var/log\")
add_definitions(-DRUNDIR=\"${CMAKE_INSTALL_PREFIX}/run\")

CONFIGURE_FILE(${CMAKE_CURRENT_SOURCE_DIR}/config.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/config.h)

# Define a library called udpst_core containing all core functionality
add_library(udpst_core udpst_control.c udpst_data.c udpst_srates.c cJSON.c)
set(libraries udpst_core ${libraries})

add_executable(udpst udpst.c)
target_link_libraries(udpst ${libraries} m)

# For some reason Ninja sometimes faces a stupid error which is fixed by
# the following
if (CMAKE_GENERATOR MATCHES "Ninja")
    set(CMAKE_PLATFORM_HAS_INSTALLNAME 1)
endif()
