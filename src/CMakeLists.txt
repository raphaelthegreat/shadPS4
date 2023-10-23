# Silence "deprecation" warnings
add_definitions(-D_CRT_SECURE_NO_WARNINGS -D_CRT_NONSTDC_NO_DEPRECATE -D_SCL_SECURE_NO_WARNINGS)

add_executable(shadps4
    common/debug.h
    common/disassembler.cpp
    common/disassembler.h
    common/discord.cpp
    common/discord.h
    common/fs_file.cpp
    common/fs_file.h
    common/log.cpp
    common/log.h
    common/string_util.cpp
    common/string_util.h
    common/types.h
    common/version.h
    common/virtual_memory.cpp
    common/virtual_memory.h
    core/emulator.cpp
    core/emulator.h
    core/linker.cpp
    core/linker.h
    core/aerolib/aerolib.cpp
    core/aerolib/aerolib.h
    core/aerolib/aerolib.inl
    core/aerolib/stubs.cpp
    core/aerolib/stubs.h
    core/file_sys/generic_file_system.h
    core/file_sys/meta_file_system.cpp
    core/file_sys/meta_file_system.h
    core/frontend/emu_window.h
    core/hle/error_codes.h
    core/hle/kernel/cpu_management.cpp
    core/hle/kernel/cpu_management.h
    core/hle/kernel/event_queues.cpp
    core/hle/kernel/event_queues.h
    core/hle/kernel/memory_management.cpp
    core/hle/kernel/memory_management.h
    core/hle/kernel/thread_management.cpp
    core/hle/kernel/thread_management.h
    core/hle/kernel/objects/event_queue.cpp
    core/hle/kernel/objects/event_queue.h
    core/hle/kernel/objects/physical_memory.cpp
    core/hle/kernel/objects/physical_memory.h
    core/hle/libraries/libs.cpp
    core/hle/libraries/libs.h
    core/hle/libraries/libc/libc.cpp
    core/hle/libraries/libc/libc.h
    core/hle/libraries/libc/libc_cxa.cpp
    core/hle/libraries/libc/libc_cxa.h
    core/hle/libraries/libc/printf.h
    core/hle/libraries/libc/va_ctx.h
    core/hle/libraries/libkernel/libkernel.cpp
    core/hle/libraries/libkernel/libkernel.h
    core/hle/libraries/libpad/pad.cpp
    core/hle/libraries/libpad/pad.h
    core/hle/libraries/libscegnmdriver/libscegnmdriver.cpp
    core/hle/libraries/libscegnmdriver/libscegnmdriver.h
    core/hle/libraries/libscevideoout/gpu_memory.cpp
    core/hle/libraries/libscevideoout/gpu_memory.h
    core/hle/libraries/libscevideoout/video_out.cpp
    core/hle/libraries/libscevideoout/video_out.h
    core/hle/libraries/libscevideoout/video_out_buffer.cpp
    core/hle/libraries/libscevideoout/video_out_buffer.h
    core/hle/libraries/libsystemservice/system_service.cpp
    core/hle/libraries/libsystemservice/system_service.h
    core/hle/libraries/libuserservice/user_service.cpp
    core/hle/libraries/libuserservice/user_service.h
    core/hle/libraries/libuserservice/usr_mng_codes.h
    core/input/controller.cpp
    core/input/controller.h
    core/loader/elf.cpp
    core/loader/elf.h
    core/loader/symbols_resolver.cpp
    core/loader/symbols_resolver.h
    shad_sdl/config.cpp
    shad_sdl/config.h
    shad_sdl/elf_viewer.cpp
    shad_sdl/elf_viewer.h
    shad_sdl/emu_window_sdl.cpp
    shad_sdl/emu_window_sdl.h
    shad_sdl/main.cpp
    video_core/graphics_ctx.h
    video_core/graphics_render.cpp
    video_core/graphics_render.h
    video_core/tile_manager.cpp
    video_core/tile_manager.h
    video_core/video_out_ctx.cpp
    video_core/video_out_ctx.h
    video_core/vulkan_util.cpp
    video_core/vulkan_util.h
)

create_target_directory_groups(shadps4)
target_link_libraries(shadps4 PRIVATE fmt mincore winpthread spdlog imgui SDL3-shared zydis toml11::toml11)
target_include_directories(shadps4 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_custom_command(TARGET shadps4 POST_BUILD
   COMMAND ${CMAKE_COMMAND} -E copy_if_different
     $<TARGET_FILE:SDL3-shared>
     $<TARGET_FILE_DIR:shadps4>)
add_custom_command(TARGET shadps4 POST_BUILD
   COMMAND ${CMAKE_COMMAND} -E copy_if_different
     "${PROJECT_SOURCE_DIR}/third-party/winpthread/bin/libwinpthread-1.dll" $<TARGET_FILE_DIR:shadps4>)
