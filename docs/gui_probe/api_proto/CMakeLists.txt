cmake_minimum_required(VERSION 3.20)
cmake_policy(SET CMP0091 NEW)
project(GuiApiProto LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
# What the launcher's plugin handbook recommends for plugins: the static C runtime. The host (../showcase) uses the default (dynamic) one, so
# this also exercises "no heap, no C++ object crosses the boundary".
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

set(IMGUI_DIR "" CACHE PATH "Dear ImGui v1.85 source tree (only consumer A compiles it)")
if (NOT IMGUI_DIR)
    message(FATAL_ERROR "pass -DIMGUI_DIR=<path to an ImGui v1.85 checkout>")
endif()

add_library(consumer_a SHARED consumer_a.cpp
    ${IMGUI_DIR}/imgui.cpp ${IMGUI_DIR}/imgui_draw.cpp ${IMGUI_DIR}/imgui_tables.cpp ${IMGUI_DIR}/imgui_widgets.cpp)
target_include_directories(consumer_a PRIVATE ${IMGUI_DIR} ${CMAKE_CURRENT_LIST_DIR})
target_compile_definitions(consumer_a PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
set_target_properties(consumer_a PROPERTIES PREFIX "")

# the same plugin built with another imconfig: 32-bit draw indices, a common choice that breaks the engine's 16-bit index buffers.
# The host has to be able to tell, and the plugin must refuse to draw.
add_library(consumer_abad SHARED consumer_a.cpp
    ${IMGUI_DIR}/imgui.cpp ${IMGUI_DIR}/imgui_draw.cpp ${IMGUI_DIR}/imgui_tables.cpp ${IMGUI_DIR}/imgui_widgets.cpp)
target_include_directories(consumer_abad PRIVATE ${IMGUI_DIR} ${CMAKE_CURRENT_LIST_DIR})
target_compile_definitions(consumer_abad PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS "ImDrawIdx=unsigned int" CONSUMER_TAG="abad")
set_target_properties(consumer_abad PROPERTIES PREFIX "")

# no ImGui anywhere: plain C
add_library(consumer_b SHARED consumer_b.c)
target_include_directories(consumer_b PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_compile_definitions(consumer_b PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
set_target_properties(consumer_b PROPERTIES PREFIX "")

if (MSVC)
    target_compile_options(consumer_a PRIVATE /utf-8)
endif()
