# SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

cmake_minimum_required(VERSION 3.16.3)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

project(shadps4)

# This function should be passed a list of all files in a target. It will automatically generate
# file groups following the directory hierarchy, so that the layout of the files in IDEs matches the
# one in the filesystem.
function(create_target_directory_groups target_name)
    # Place any files that aren't in the source list in a separate group so that they don't get in
    # the way.
    source_group("Other Files" REGULAR_EXPRESSION ".")

    get_target_property(target_sources "${target_name}" SOURCES)

    foreach(file_name IN LISTS target_sources)
        get_filename_component(dir_name "${file_name}" PATH)
        # Group names use '\' as a separator even though the entire rest of CMake uses '/'...
        string(REPLACE "/" "\\" group_name "${dir_name}")
        source_group("${group_name}" FILES "${file_name}")
    endforeach()
endfunction()


# Setup a custom clang-format target (if clang-format can be found) that will run
# against all the src files. This should be used before making a pull request.
# =======================================================================

set(CLANG_FORMAT_POSTFIX "-15")
find_program(CLANG_FORMAT
    NAMES clang-format${CLANG_FORMAT_POSTFIX}
          clang-format
    PATHS ${PROJECT_BINARY_DIR}/externals)
# if find_program doesn't find it, try to download from externals
if (NOT CLANG_FORMAT)
    if (WIN32)
        message(STATUS "Clang format not found! Downloading...")
        set(CLANG_FORMAT "${PROJECT_BINARY_DIR}/externals/clang-format${CLANG_FORMAT_POSTFIX}.exe")
        file(DOWNLOAD
            https://github.com/citra-emu/ext-windows-bin/raw/master/clang-format${CLANG_FORMAT_POSTFIX}.exe
            "${CLANG_FORMAT}" SHOW_PROGRESS
            STATUS DOWNLOAD_SUCCESS)
        if (NOT DOWNLOAD_SUCCESS EQUAL 0)
            message(WARNING "Could not download clang format! Disabling the clang format target")
            file(REMOVE ${CLANG_FORMAT})
            unset(CLANG_FORMAT)
        endif()
    else()
        message(WARNING "Clang format not found! Disabling the clang format target")
    endif()
endif()

if (CLANG_FORMAT)
    set(SRCS ${PROJECT_SOURCE_DIR}/src)
    set(CCOMMENT "Running clang format against all the .h and .cpp files in src/")
    if (WIN32)
        if(MINGW)
            add_custom_target(clang-format
                COMMAND find `cygpath -u ${SRCS}` -iname *.h -o -iname *.cpp -o -iname *.mm | xargs `cygpath -u ${CLANG_FORMAT}` -i
                COMMENT ${CCOMMENT})
        else()
            add_custom_target(clang-format
                COMMAND powershell.exe -Command "Get-ChildItem '${SRCS}/*' -Include *.cpp,*.h,*.mm -Recurse | Foreach {&'${CLANG_FORMAT}' -i $_.fullname}"
                COMMENT ${CCOMMENT})
        endif()
    else()
        add_custom_target(clang-format
            COMMAND find ${SRCS} -iname *.h -o -iname *.cpp -o -iname *.mm | xargs ${CLANG_FORMAT} -i
            COMMENT ${CCOMMENT})
    endif()
    unset(SRCS)
    unset(CCOMMENT)
endif()

add_subdirectory(externals)
add_subdirectory(third-party)
include_directories(src)

set(LIBRARIES src/core/libraries/error_codes.h
              src/core/libraries/gnmdriver/libscegnmdriver_new.cpp
              src/core/libraries/gnmdriver/libscegnmdriver_new.h
)
set(LIBC_SOURCES src/core/libraries/libc/libc.cpp
                 src/core/libraries/libc/libc.h
                 src/core/libraries/libc/printf.h
                 src/core/libraries/libc/va_ctx.h
                 src/core/libraries/libc/libc_cxa.cpp
                 src/core/libraries/libc/libc_cxa.h
                 src/core/libraries/libc/libc_stdio.cpp
                 src/core/libraries/libc/libc_stdio.h
                 src/core/libraries/libc/libc_math.cpp
                 src/core/libraries/libc/libc_math.h
                 src/core/libraries/libc/libc_string.cpp
                 src/core/libraries/libc/libc_string.h
                 src/core/libraries/libc/libc_stdlib.cpp
                 src/core/libraries/libc/libc_stdlib.h
)
set(USERSERVICE_SOURCES src/core/libraries/userservice/libuserservice.cpp
                        src/core/libraries/userservice/libuserservice.h
)

set(PAD_SOURCES src/core/libraries/pad/pad.cpp
                src/core/libraries/pad/pad.h
)

set(SYSTEMSERVICE_SOURCES src/core/libraries/systemservice/system_service.cpp
                          src/core/libraries/systemservice/system_service.h
)

set(FILESYSTEM_SOURCES src/core/libraries/kernel/file_system.cpp
                       src/core/libraries/kernel/file_system.h
                       src/core/file_sys/fs.cpp
                       src/core/file_sys/fs.h
)

set(RENDERER_VULKAN src/video_core/renderer_vulkan/vk_common.cpp
                    src/video_core/renderer_vulkan/vk_common.h
                    src/video_core/renderer_vulkan/vk_descriptor_update_queue.cpp
                    src/video_core/renderer_vulkan/vk_descriptor_update_queue.h
                    src/video_core/renderer_vulkan/vk_instance.cpp
                    src/video_core/renderer_vulkan/vk_instance.h
                    src/video_core/renderer_vulkan/vk_master_semaphore.cpp
                    src/video_core/renderer_vulkan/vk_master_semaphore.h
                    src/video_core/renderer_vulkan/vk_platform.cpp
                    src/video_core/renderer_vulkan/vk_platform.h
                    src/video_core/renderer_vulkan/vk_resource_pool.cpp
                    src/video_core/renderer_vulkan/vk_resource_pool.h
                    src/video_core/renderer_vulkan/vk_scheduler.cpp
                    src/video_core/renderer_vulkan/vk_scheduler.h
                    src/video_core/renderer_vulkan/vk_shader_util.cpp
                    src/video_core/renderer_vulkan/vk_shader_util.h
                    src/video_core/renderer_vulkan/vk_stream_buffer.cpp
                    src/video_core/renderer_vulkan/vk_stream_buffer.h
                    src/video_core/renderer_vulkan/vk_swapchain.cpp
                    src/video_core/renderer_vulkan/vk_swapchain.h)

add_executable(shadps4
    src/common/alignment.h
    src/common/assert.cpp
    src/common/assert.h
    src/common/bounded_threadsafe_queue.h
    src/common/concepts.h
    src/common/debug.h
    src/common/disassembler.cpp
    src/common/disassembler.h
    src/common/discord.cpp
    src/common/discord.h
    src/common/error.cpp
    src/common/error.h
    #src/common/host_memory.cpp
    src/common/host_memory.h
    src/common/io_file.cpp
    src/common/io_file.h
    src/common/path_util.cpp
    src/common/path_util.h
    src/common/logging/backend.cpp
    src/common/logging/backend.h
    src/common/logging/filter.cpp
    src/common/logging/filter.h
    src/common/logging/formatter.h
    src/common/logging/log_entry.h
    src/common/logging/log.h
    src/common/logging/text_formatter.cpp
    src/common/logging/text_formatter.h
    src/common/logging/types.h
    src/common/native_clock.cpp
    src/common/native_clock.h
    src/common/rdtsc.cpp
    src/common/rdtsc.h
    src/common/scope_exit.h
    src/common/singleton.h
    src/common/string_util.cpp
    src/common/string_util.h
    src/common/thread.cpp
    src/common/thread.h
    src/common/types.h
    src/common/uint128.h
    src/common/version.h
    src/common/virtual_buffer.cpp
    src/common/virtual_buffer.h
    ${LIBC_SOURCES}
    ${USERSERVICE_SOURCES}
    ${PAD_SOURCES}
    ${SYSTEMSERVICE_SOURCES}
    ${FILESYSTEM_SOURCES}
    ${LIBRARIES}
    ${RENDERER_VULKAN}
    src/input/controller.cpp
    src/input/controller.h
    src/main.cpp
    src/core/loader/elf.cpp
    src/core/loader/elf.h
    src/common/config.cpp
    src/common/config.h
    src/core/virtual_memory.cpp
    src/core/virtual_memory.h
    src/core/linker.cpp
    src/core/linker.h
    src/core/aerolib/stubs.cpp
    src/core/aerolib/stubs.h
    src/core/aerolib/aerolib.cpp
    src/core/aerolib/aerolib.h
    src/core/libraries/kernel/physical_memory.h
    src/core/libraries/kernel/physical_memory.cpp
    src/core/libraries/kernel/event_queues.cpp
    src/core/libraries/kernel/event_queues.h
    src/core/libraries/kernel/cpu_management.cpp
    src/core/libraries/kernel/cpu_management.h
    src/core/loader/symbols_resolver.h
    src/core/loader/symbols_resolver.cpp
    src/core/libraries/libs.cpp
    src/core/libraries/libs.h
    src/core/libraries/kernel/libkernel.cpp
    src/core/libraries/kernel/libkernel.h
    src/core/libraries/gnmdriver/libscegnmdriver.cpp
    src/core/libraries/gnmdriver/libscegnmdriver.h
    src/core/libraries/videoout/buffer.h
    src/core/libraries/videoout/driver.cpp
    src/core/libraries/videoout/driver.h
    src/core/libraries/videoout/video_out.cpp
    src/core/libraries/videoout/video_out.h
    src/core/libraries/kernel/thread_management.cpp
    src/core/libraries/kernel/thread_management.h
    src/core/libraries/kernel/memory_management.cpp
    src/core/libraries/kernel/memory_management.h
    src/core/libraries/error_codes.h
    src/emulator.cpp
    src/emulator.h
    src/core/libraries/kernel/event_queue.h
    src/core/libraries/kernel/event_queue.cpp
    src/video_core/texture_cache/slot_vector.h
    src/video_core/texture_cache/tile_manager.cpp
    src/video_core/texture_cache/tile_manager.h
    src/core/libraries/kernel/time_management.cpp
    src/core/libraries/kernel/time_management.h
    src/core/tls.cpp
    src/core/tls.h
    src/sdl_window.cpp
    src/sdl_window.h
    src/video_core/texture_cache/image.cpp
    src/video_core/texture_cache/image.h
    src/video_core/pixel_format.h
    src/video_core/texture_cache/types.h
    src/video_core/texture_cache/texture_cache.cpp
    src/video_core/texture_cache/texture_cache.h
    src/video_core/texture_cache/image_view.cpp
    src/video_core/texture_cache/image_view.h
    src/common/enum.h
    src/video_core/renderer_vulkan/renderer_vulkan.h src/video_core/renderer_vulkan/renderer_vulkan.cpp
)

create_target_directory_groups(shadps4)

target_link_libraries(shadps4 PRIVATE magic_enum::magic_enum fmt::fmt toml11::toml11 tsl::robin_map)
target_link_libraries(shadps4 PRIVATE discord-rpc boost SDL3-static vma vulkan-headers xxhash Zydis SPIRV glslang)
if (WIN32)
    target_link_libraries(shadps4 PRIVATE mincore winpthread clang_rt.builtins-x86_64.lib)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS -D_CRT_NONSTDC_NO_DEPRECATE -D_SCL_SECURE_NO_WARNINGS)
    add_definitions(-DNOMINMAX -DWIN32_LEAN_AND_MEAN)
endif()

if (WIN32)
    add_custom_command(TARGET shadps4 POST_BUILD
       COMMAND ${CMAKE_COMMAND} -E copy_if_different
         $<TARGET_FILE:SDL3-shared>
         $<TARGET_FILE_DIR:shadps4>)
    add_custom_command(TARGET shadps4 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
     "${PROJECT_SOURCE_DIR}/third-party/winpthread/bin/libwinpthread-1.dll" $<TARGET_FILE_DIR:shadps4>)
endif()
