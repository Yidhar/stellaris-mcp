cmake_minimum_required(VERSION 3.20)
project(GuiPoc LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
include(FetchContent)
FetchContent_Declare(minhook GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git GIT_TAG v1.3.4)
FetchContent_MakeAvailable(minhook)

set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../imgui185)
add_library(gui_poc SHARED
    poc.cpp
    ${IMGUI_DIR}/imgui.cpp ${IMGUI_DIR}/imgui_draw.cpp ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp ${IMGUI_DIR}/imgui_demo.cpp)
target_include_directories(gui_poc PRIVATE ${IMGUI_DIR})
target_link_libraries(gui_poc PRIVATE minhook d3d11 dxgi)
target_compile_definitions(gui_poc PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
if (MSVC)
    target_compile_options(gui_poc PRIVATE /utf-8)
endif()
set_target_properties(gui_poc PROPERTIES PREFIX "")
