cmake_minimum_required(VERSION 3.20)

project(EnvLauncher VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable Link-Time Optimization (LTO) in Release mode
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)

# Enable Unicode API across all targets
add_compile_definitions(UNICODE _UNICODE)

if (MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
    if (MINGW)
        add_link_options(-municode -static)
    endif()
endif()

# Core static library
add_library(envlauncher_core STATIC
    third_party/yyjson.h
    third_party/yyjson.c
    src/util/Win32Error.h
    src/util/Win32Error.cpp
    src/util/StringUtils.h
    src/util/StringUtils.cpp
    src/model/EnvironmentProfile.h
    src/model/AppProfile.h
    src/model/ConfigModel.h
    src/process/EnvironmentBuilder.h
    src/process/EnvironmentBuilder.cpp
    src/process/ProcessLauncher.h
    src/process/ProcessLauncher.cpp
    src/config/ConfigManager.h
    src/config/ConfigManager.cpp
    src/app/ConfiguredAppLauncher.h
    src/app/ConfiguredAppLauncher.cpp
    src/platform/DpiHelper.h
    src/platform/DpiHelper.cpp
    src/platform/DwmHelper.h
    src/platform/DwmHelper.cpp
    src/platform/IconHelper.h
    src/platform/IconHelper.cpp
    src/ui/Theme.h
    src/ui/Controls.h
    src/ui/Layout.h
    src/ui/Layout.cpp
    src/ui/Renderer.h
    src/ui/Renderer.cpp
    src/ui/FileDialog.h
    src/ui/FileDialog.cpp
    src/ui/AppEditDialog.h
    src/ui/AppEditDialog.cpp
    src/ui/ProfileEditDialog.h
    src/ui/ProfileEditDialog.cpp
    src/ui/ProfileManagerDialog.h
    src/ui/ProfileManagerDialog.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
    src/app/Application.h
    src/app/Application.cpp
)

target_include_directories(envlauncher_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

# Release size optimization: core library also needs these flags for GC sections to work
if (MINGW)
    target_compile_options(envlauncher_core PRIVATE
        $<$<CONFIG:Release>:
            -O2
            -ffunction-sections
            -fdata-sections
        >
    )
endif()

target_compile_definitions(envlauncher_core PRIVATE
    YYJSON_DISABLE_INCR_READER=1
    YYJSON_DISABLE_UTILS=1
    YYJSON_DISABLE_NON_STANDARD=1
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_link_libraries(envlauncher_core PUBLIC
    d2d1
    dwrite
    dwmapi
    shell32
    ole32
    user32
    gdi32
    comctl32
    uxtheme
)

# Compile Windows resource (manifest)
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/EnvLauncher.res.obj
    COMMAND windres -O coff -i "${CMAKE_CURRENT_SOURCE_DIR}/src/EnvLauncher.rc" -o "${CMAKE_CURRENT_BINARY_DIR}/EnvLauncher.res.obj"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/EnvLauncher.rc" "${CMAKE_CURRENT_SOURCE_DIR}/src/app.manifest" "${CMAKE_CURRENT_SOURCE_DIR}/src/app.ico"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/src"
    VERBATIM
)

# Main Application Executable (GUI)
add_executable(EnvLauncher WIN32
    src/main.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/EnvLauncher.res.obj
)

target_link_libraries(EnvLauncher PRIVATE
    envlauncher_core
)

if(MINGW)
    target_compile_options(EnvLauncher PRIVATE
        $<$<CONFIG:Release>:
            -O2
            -ffunction-sections
            -fdata-sections
        >
    )
    target_link_options(EnvLauncher PRIVATE
        "-specs=${CMAKE_CURRENT_SOURCE_DIR}/cmake/no-default-manifest.specs"
        $<$<CONFIG:Release>:
            -Wl,--gc-sections
            -Wl,--strip-all
        >
    )
endif()

# Test helper probe executable
add_executable(env_probe
    tests/env_probe.cpp
)

# Phase 1 Test runner
add_executable(test_phase1
    tests/test_phase1.cpp
)

target_link_libraries(test_phase1 PRIVATE
    envlauncher_core
)

# Phase 2 Test runner
add_executable(test_phase2
    tests/test_phase2.cpp
)

target_link_libraries(test_phase2 PRIVATE
    envlauncher_core
)

# Phase 3 Test runner
add_executable(test_phase3
    tests/test_phase3.cpp
)

target_link_libraries(test_phase3 PRIVATE
    envlauncher_core
)

# Phase 4 Test runner
add_executable(test_phase4
    tests/test_phase4.cpp
)

target_link_libraries(test_phase4 PRIVATE
    envlauncher_core
)

# Phase 5 Test runner
add_executable(test_phase5
    tests/test_phase5.cpp
)

target_link_libraries(test_phase5 PRIVATE
    envlauncher_core
)

# Phase 6 Test runner
add_executable(test_phase6
    tests/test_phase6.cpp
)

target_link_libraries(test_phase6 PRIVATE
    envlauncher_core
)

# Make sure env_probe is built before tests run
add_dependencies(test_phase1 env_probe)
add_dependencies(test_phase2 env_probe)
add_dependencies(test_phase4 env_probe)
add_dependencies(test_phase5 env_probe)
add_dependencies(test_phase6 env_probe)

enable_testing()
add_test(NAME Phase1Tests COMMAND test_phase1)
add_test(NAME Phase2Tests COMMAND test_phase2)
add_test(NAME Phase3Tests COMMAND test_phase3)
add_test(NAME Phase4Tests COMMAND test_phase4)
add_test(NAME Phase5Tests COMMAND test_phase5)
add_test(NAME Phase6Tests COMMAND test_phase6)



