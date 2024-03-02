# SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

if (MSVC)
    # Silence "deprecation" warnings
    add_definitions(-D_CRT_SECURE_NO_WARNINGS -D_CRT_NONSTDC_NO_DEPRECATE -D_SCL_SECURE_NO_WARNINGS)
endif()

# Boost
set(BOOST_ROOT "${CMAKE_SOURCE_DIR}/externals/boost" CACHE STRING "")
set(Boost_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/externals/boost" CACHE STRING "")
set(Boost_NO_SYSTEM_PATHS ON CACHE BOOL "")
add_library(boost INTERFACE)
target_include_directories(boost SYSTEM INTERFACE ${Boost_INCLUDE_DIR})

# Discord-RPC
set(BUILD_EXAMPLES OFF CACHE BOOL "")
add_subdirectory(discord-rpc EXCLUDE_FROM_ALL)
target_include_directories(discord-rpc INTERFACE ./discord-rpc/include)

# SDL3
set(SDL_SHARED OFF)
set(SDL_STATIC ON)
add_subdirectory(SDL EXCLUDE_FROM_ALL)

# vulkan-headers
add_library(vulkan-headers INTERFACE)
target_include_directories(vulkan-headers SYSTEM INTERFACE ./vulkan-headers/include)

# VMA
add_library(vma INTERFACE)
target_include_directories(vma SYSTEM INTERFACE ./vma/include)

# glslang
set(SKIP_GLSLANG_INSTALL ON CACHE BOOL "")
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "")
set(ENABLE_SPVREMAPPER OFF CACHE BOOL "")
set(ENABLE_CTEST OFF CACHE BOOL "")
set(ENABLE_HLSL OFF CACHE BOOL "")
set(BUILD_EXTERNAL OFF CACHE BOOL "")
set(ENABLE_OPT OFF CACHE BOOL "")
add_subdirectory(glslang)

# Robin-map
add_subdirectory(robin-map)
