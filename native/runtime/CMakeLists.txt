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
    src/bridge/DebugFiles.cpp
    src/bridge/GameBridge.cpp
    src/bridge/LoopbackBridge.cpp
    src/bridge/AgentState.cpp
    src/kotlin/KotlinBridge.cpp
    src/memory/ComponentWalk.cpp
    src/memory/NativeMemory.cpp
    src/modules/FlyInput.cpp
    src/modules/MelodyModules.cpp
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
    src/bridge/DebugFiles.cpp
    src/bridge/GameBridge.cpp
    src/bridge/AgentState.cpp
    src/kotlin/KotlinBridge.cpp
    src/memory/ComponentWalk.cpp
    src/memory/NativeMemory.cpp
    src/modules/FlyInput.cpp
    src/modules/MelodyModules.cpp
    src/modules/ModuleManager.cpp
    src/modules/GameplayModules.cpp
    src/profile/PatchManager.cpp
    ../vendor/arm64hook/And64InlineHook.cpp
)
target_include_directories(eclient_host PRIVATE
    src
    ../vendor/dobby
    ../vendor/arm64hook
)
target_compile_options(eclient_host PRIVATE -Wall -Wextra -fvisibility=hidden -fvisibility-inlines-hidden)
target_link_options(eclient_host PRIVATE "-Wl,-z,max-page-size=16384")

find_library(log-lib log REQUIRED)

set(DOBBY_LIB ${CMAKE_CURRENT_SOURCE_DIR}/../vendor/dobby/libdobby.a)

target_link_libraries(eclient_runtime PRIVATE
    ${log-lib}
    ${DOBBY_LIB}
)
target_link_libraries(eclient_host PRIVATE
    ${log-lib}
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
