cmake_minimum_required(VERSION 3.20)

project(AccelTool LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

set(MSCL_ROOT ${CMAKE_SOURCE_DIR}/third_party/mscl)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(MSCL_LIB ${MSCL_ROOT}/lib/debug/MSCL.lib)
    set(MSCL_DLL ${MSCL_ROOT}/bin/debug/MSCL.dll)
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(MSCL_LIB ${MSCL_ROOT}/lib/release/MSCL.lib)
    set(MSCL_DLL ${MSCL_ROOT}/bin/release/MSCL.dll)
else()
    message(FATAL_ERROR "Unsupported build type: ${CMAKE_BUILD_TYPE}")
endif()

message(STATUS "Using MSCL lib: ${MSCL_LIB}")
message(STATUS "Using MSCL dll: ${MSCL_DLL}")

add_library(acceltool_core
    src/backend/wireless_accelerometer_manager.cpp
    src/core/app_config.cpp
    src/core/interactive_app.cpp
    src/core/sampling_session.cpp
    src/processing/magnitude_calculator.cpp
    src/processing/display_aggregator.cpp
    src/utils/csv_writer.cpp
    src/utils/display_csv_writer.cpp
    src/utils/logger.cpp
)

target_include_directories(acceltool_core
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${MSCL_ROOT}/include
)

target_link_libraries(acceltool_core
    PUBLIC
        ${MSCL_LIB}
)

add_executable(AccelTool
    src/main.cpp
)

target_link_libraries(AccelTool
    PRIVATE
        acceltool_core
)

add_custom_command(TARGET AccelTool POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${MSCL_DLL}
        $<TARGET_FILE_DIR:AccelTool>
)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    target_compile_options(acceltool_core PRIVATE /W4 /permissive-)
    target_compile_options(AccelTool PRIVATE /W4 /permissive-)
else()
    target_compile_options(acceltool_core PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(AccelTool PRIVATE -Wall -Wextra -Wpedantic)
endif()
