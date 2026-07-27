# CMakeLists.txt
# iPhone USB Microphone - Windows Top-Level Build
#
# Build instructions:
#   mkdir build && cd build
#   cmake .. -G "Visual Studio 17 2022" -A x64
#   cmake --build . --config Release
#
# Or use the provided build_all.bat script.

cmake_minimum_required(VERSION 3.25)
project(iPhoneUSBMicrophone VERSION 1.0.0 LANGUAGES CXX)

# C++20 standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Common include directories
set(COMMON_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/common/include)

# ============================================================================
# Common Library (protocol, ring buffer, audio format)
# ============================================================================

add_library(iphone_mic_common STATIC
    common/src/protocol.cpp
    common/src/usbmux_client.cpp
)

target_include_directories(iphone_mic_common PUBLIC
    ${COMMON_INCLUDE_DIR}
)

# ============================================================================
# Test Client (standalone TCP client for testing)
# ============================================================================

add_executable(iphone_mic_client
    client/src/main.cpp
    client/src/usb_audio_client.cpp
)

target_include_directories(iphone_mic_client PRIVATE
    ${COMMON_INCLUDE_DIR}
    client/include
)

target_link_libraries(iphone_mic_client PRIVATE
    iphone_mic_common
    ws2_32
)

# High performance settings
target_compile_definitions(iphone_mic_client PRIVATE
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
    NOMINMAX
)

# ============================================================================
# ASIO Driver DLL
# ============================================================================

add_library(iphone_asio_driver SHARED
    asio_driver/src/iphone_asio_driver.cpp
    asio_driver/src/asio_com_factory.cpp
    asio_driver/src/dllmain.cpp
    asio_driver/src/register.cpp
)

target_include_directories(iphone_asio_driver PRIVATE
    ${COMMON_INCLUDE_DIR}
    asio_driver/include
    third_party/ASIOSDK/ASIOSDK/common
)

target_link_libraries(iphone_asio_driver PRIVATE
    iphone_mic_common
    ws2_32
    winmm
    shlwapi
    ole32
)

target_compile_definitions(iphone_asio_driver PRIVATE
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
    NOMINMAX
    NATIVE_INT64=1
)

# Module definition file for DLL exports
target_sources(iphone_asio_driver PRIVATE
    asio_driver/src/iphone_asio_driver.def
)

# ============================================================================
# Tests (Ring Buffer, Protocol)
# ============================================================================

# Ring Buffer Test
add_executable(ring_buffer_test
    tests/ring_buffer_test.cpp
)
target_include_directories(ring_buffer_test PRIVATE ${COMMON_INCLUDE_DIR})

# Protocol Test
add_executable(protocol_test
    tests/protocol_test.cpp
    common/src/protocol.cpp
)
target_include_directories(protocol_test PRIVATE ${COMMON_INCLUDE_DIR})

# Audio Format Test
add_executable(audio_format_test
    tests/audio_format_test.cpp
)
target_include_directories(audio_format_test PRIVATE ${COMMON_INCLUDE_DIR})

# ============================================================================
# GUI Application
# ============================================================================
add_subdirectory(gui)

# ============================================================================
# Compiler Options
# ============================================================================

# MSVC-specific optimization flags
if(MSVC)
    # All targets: warnings and optimizations
    add_compile_options(/W3 /MP /Zc:__cplusplus)
    
    # Release: full optimization
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2 /Oi /GL")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LTCG")
    set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /LTCG")
endif()

# ============================================================================
# Install
# ============================================================================

install(TARGETS iphone_mic_client iphone_asio_driver iphone_mic_gui
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
)

# Print summary
message(STATUS "========================================")
message(STATUS "iPhone USB Microphone - Windows Build")
message(STATUS "========================================")
message(STATUS "Generator:    ${CMAKE_GENERATOR}")
message(STATUS "Architecture: ${CMAKE_GENERATOR_PLATFORM}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "========================================")
