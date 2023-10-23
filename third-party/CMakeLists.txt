# discord-rpc
add_subdirectory(discord-rpc)
target_include_directories(discord-rpc INTERFACE ./discord-rpc/include)

# fmtlib
option(FMT_INSTALL "" ON)
add_subdirectory(fmt)

# spdlog
set(SPDLOG_WCHAR_FILENAMES ON CACHE BOOL "")
set(SPDLOG_NO_THREAD_ID ON CACHE BOOL "")
set(SPDLOG_FMT_EXTERNAL ON CACHE BOOL "")
add_subdirectory(spdlog EXCLUDE_FROM_ALL)
add_library(stb INTERFACE)
target_include_directories(stb INTERFACE stb)

# SDL3
add_subdirectory(SDL)

# winpthreads
add_subdirectory(winpthread)

# toml11
add_subdirectory(toml11)

# vulkan
add_subdirectory(vulkan)

# zydis
# Register Zydis dependency.
# Disable build of tools and examples.
option(ZYDIS_BUILD_TOOLS "" OFF)
option(ZYDIS_BUILD_EXAMPLES "" OFF)
add_subdirectory(zydis)

# imgui
add_library(imgui STATIC)
target_sources(imgui PRIVATE
    imgui/imgui_demo.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_tables.cpp
    imgui/imgui_widgets.cpp
    imgui/imgui.cpp
    imgui/backends/imgui_impl_opengl3.cpp
    imgui/backends/imgui_impl_sdl3.cpp
)
target_include_directories(imgui PUBLIC imgui imgui/backends SDL/include)
target_link_libraries(imgui PUBLIC SDL3-shared ${CMAKE_DL_LIBS} Zydis winpthread discord-rpc)
