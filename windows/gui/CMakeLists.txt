# GUI Tool CMakeLists.txt
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)

add_executable(iphone_mic_gui WIN32
    src/main_gui.cpp
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
)

target_include_directories(iphone_mic_gui PRIVATE
    ${COMMON_INCLUDE_DIR}
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

target_link_libraries(iphone_mic_gui PRIVATE
    iphone_mic_common
    d3d11
    d3dcompiler
    ws2_32
)

target_compile_definitions(iphone_mic_gui PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)
