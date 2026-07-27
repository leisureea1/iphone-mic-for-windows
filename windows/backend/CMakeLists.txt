# Backend DLL CMakeLists.txt

add_library(iphone_mic_backend SHARED
    src/backend.cpp
)

target_include_directories(iphone_mic_backend PRIVATE
    ${COMMON_INCLUDE_DIR}
    ${CMAKE_SOURCE_DIR}/third_party/miniaudio
)

target_link_libraries(iphone_mic_backend PRIVATE
    iphone_mic_common
    ws2_32
    Advapi32
)

target_compile_definitions(iphone_mic_backend PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    IPHONE_MIC_BACKEND_EXPORTS
)
