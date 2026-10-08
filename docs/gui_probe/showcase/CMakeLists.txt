cmake_minimum_required(VERSION 3.20)
project(GuiShowcase LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
include(FetchContent)
FetchContent_Declare(minhook GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git GIT_TAG v1.3.4)
FetchContent_MakeAvailable(minhook)

# Dear ImGui 1.85 (the version the 4.5.2 exe ships): `git clone --branch v1.85 https://github.com/ocornut/imgui` and pass -DIMGUI_DIR=<clone>.
set(IMGUI_DIR "" CACHE PATH "Dear ImGui v1.85 source tree")
if (NOT IMGUI_DIR)
    message(FATAL_ERROR "pass -DIMGUI_DIR=<path to an ImGui v1.85 checkout>")
endif()
# The generated SDK header of the main repository (exe timestamp + RVAs + command specs).
set(SDK_INCLUDE ${CMAKE_CURRENT_LIST_DIR}/../../../stellaris_bridge/include)

add_library(gui_showcase SHARED
    showcase.cpp
    ${IMGUI_DIR}/imgui.cpp ${IMGUI_DIR}/imgui_draw.cpp ${IMGUI_DIR}/imgui_tables.cpp ${IMGUI_DIR}/imgui_widgets.cpp)
target_include_directories(gui_showcase PRIVATE ${IMGUI_DIR} ${SDK_INCLUDE})
target_link_libraries(gui_showcase PRIVATE minhook d3d11 dxgi)
target_compile_definitions(gui_showcase PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
if (MSVC)
    target_compile_options(gui_showcase PRIVATE /utf-8)
endif()
set_target_properties(gui_showcase PROPERTIES PREFIX "")
