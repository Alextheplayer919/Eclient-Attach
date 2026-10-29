cmake_minimum_required(VERSION 3.22)
project(eclient_runtime LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT ANDROID)
    message(FATAL_ERROR "E-Client native runtime must be built with the Android NDK")
endif()
if(NOT CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a")
    message(FATAL_ERROR "E-Client native runtime currently targets arm64-v8a only")
endif()

set(ECLIENT_COMMON_SOURCES
    src/main.cpp
    src/bridge/GameBridge.cpp
    src/bridge/LoopbackBridge.cpp
    src/memory/NativeMemory.cpp
    src/modules/ModuleManager.cpp
    src/modules/GameplayModules.cpp
    src/profile/PatchManager.cpp
)

add_library(eclient_runtime SHARED ${ECLIENT_COMMON_SOURCES})
# GameplayModules.cpp includes dobby.h and calls DobbyHook — same vendor path as eclient_host.
target_include_directories(eclient_runtime PRIVATE
    src
    ../vendor/dobby
)
target_compile_options(eclient_runtime PRIVATE -Wall -Wextra -fvisibility=hidden -fvisibility-inlines-hidden)
target_link_options(eclient_runtime PRIVATE "-Wl,-z,max-page-size=16384")

add_library(eclient_host SHARED
    src/host/JniBootstrap.cpp
    src/host/MinecraftHost.cpp
    src/gui/InGameGui.cpp
    src/bridge/GameBridge.cpp
    src/memory/NativeMemory.cpp
    src/modules/ModuleManager.cpp
    src/modules/GameplayModules.cpp
    src/profile/PatchManager.cpp
    src/input/NativeInputHook.cpp
    ../vendor/imgui/imgui.cpp
    ../vendor/imgui/imgui_draw.cpp
    ../vendor/imgui/imgui_tables.cpp
    ../vendor/imgui/imgui_widgets.cpp
    ../vendor/imgui/backends/imgui_impl_opengl3.cpp
    ../vendor/imgui/backends/imgui_impl_android.cpp
    ../vendor/arm64hook/And64InlineHook.cpp
)
target_include_directories(eclient_host PRIVATE
    src
    ../vendor/imgui
    ../vendor/imgui/backends
    ../vendor/dobby
    ../vendor/arm64hook
)
target_compile_options(eclient_host PRIVATE -Wall -Wextra -fvisibility=hidden -fvisibility-inlines-hidden)
# Dear ImGui's Android/GLES backend and the Dobby/And64 sources are vendored:
# silence their warnings instead of failing the build on them.
# Minecraft Bedrock renders with GLES 3, so the ImGui renderer must be built for ES3
# (without this it falls back to the desktop-GL loader, which does not exist on Android).
set_source_files_properties(
    ../vendor/imgui/imgui.cpp
    ../vendor/imgui/imgui_draw.cpp
    ../vendor/imgui/imgui_tables.cpp
    ../vendor/imgui/imgui_widgets.cpp
    ../vendor/imgui/backends/imgui_impl_opengl3.cpp
    ../vendor/imgui/backends/imgui_impl_android.cpp
    ../vendor/arm64hook/And64InlineHook.cpp
    PROPERTIES COMPILE_FLAGS "-w")
target_compile_definitions(eclient_host PRIVATE IMGUI_IMPL_OPENGL_ES3)
target_link_options(eclient_host PRIVATE "-Wl,-z,max-page-size=16384")

find_library(log-lib log REQUIRED)
find_library(egl-lib EGL REQUIRED)
find_library(gles-lib GLESv2 REQUIRED)
find_library(gles3-lib GLESv3 REQUIRED)
find_library(android-lib android REQUIRED)

set(DOBBY_LIB ${CMAKE_CURRENT_SOURCE_DIR}/../vendor/dobby/libdobby.a)

target_link_libraries(eclient_runtime PRIVATE
    ${log-lib}
    ${DOBBY_LIB}
)
target_link_libraries(eclient_host PRIVATE
    ${log-lib}
    ${egl-lib}
    ${gles-lib}
    ${gles3-lib}
    ${android-lib}
    ${DOBBY_LIB}
)

if(CMAKE_STRIP)
    foreach(target eclient_runtime eclient_host)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_STRIP}" --strip-unneeded "$<TARGET_FILE:${target}>"
            COMMENT "Stripping ${target}"
            VERBATIM
        )
    endforeach()
endif()
