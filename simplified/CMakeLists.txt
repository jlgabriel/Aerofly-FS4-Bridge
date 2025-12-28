################################################################################
# Aerofly FS4 Reader DLL - Simplified Read-Only Build Configuration
#
# A lightweight build for the read-only version of the Aerofly Bridge.
# This version only reads data from the simulator (no control commands).
#
# Requirements:
#   - CMake 3.15+
#   - Visual Studio 2022 (MSVC)
#   - Aerofly SDK header: tm_external_message.h
#
# Build Instructions:
#   1. Copy tm_external_message.h to this directory
#   2. cmake -B build -G "Visual Studio 17 2022" -A x64
#   3. cmake --build build --config Release
#   4. Copy build/Release/AeroflyReader.dll to Aerofly external_dll folder
#
################################################################################

cmake_minimum_required(VERSION 3.15)
project(AeroflyReader VERSION 1.0.0 LANGUAGES CXX)

# C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows-specific settings
if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN)
    add_definitions(-D_WINSOCK_DEPRECATED_NO_WARNINGS)
endif()

################################################################################
# Main DLL Target
################################################################################

add_library(AeroflyReader SHARED
    aerofly_reader_dll.cpp
)

# Include current directory for tm_external_message.h
target_include_directories(AeroflyReader PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Link Windows libraries
target_link_libraries(AeroflyReader PRIVATE
    ws2_32      # Winsock2 for TCP
    kernel32
    user32
)

# MSVC-specific compiler options
if(MSVC)
    target_compile_options(AeroflyReader PRIVATE
        /W4           # Warning level 4
        /WX-          # Warnings are not errors (for flexibility)
        /MP           # Multi-processor compilation
        /Zc:__cplusplus  # Correct __cplusplus macro
    )

    # Release optimizations
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2 /DNDEBUG")

    # Define Windows version for Winsock2
    target_compile_definitions(AeroflyReader PRIVATE
        _WIN32_WINNT=0x0601  # Windows 7+
    )
endif()

# Set output name
set_target_properties(AeroflyReader PROPERTIES
    OUTPUT_NAME "AeroflyReader"
    PREFIX ""
)

################################################################################
# Installation
################################################################################

# Install to Documents/Aerofly FS 4/external_dll (optional)
install(TARGETS AeroflyReader
    RUNTIME DESTINATION external_dll
    LIBRARY DESTINATION external_dll
)

################################################################################
# Summary
################################################################################

message(STATUS "=== Aerofly Reader Build Configuration ===")
message(STATUS "Version: ${PROJECT_VERSION}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Output: AeroflyReader.dll")
message(STATUS "")
message(STATUS "Features:")
message(STATUS "  - Shared Memory: AeroflyReaderData")
message(STATUS "  - TCP Streaming: Port 12345 (configurable)")
message(STATUS "  - Read-Only: No control commands")
message(STATUS "===========================================")
