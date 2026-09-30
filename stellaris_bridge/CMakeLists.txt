add_library(stellaris_bridge SHARED
    src/dll_main.cpp
    src/hook_manager.cpp
    src/task_queue.cpp
    src/ipc_server.cpp
    src/game_state.cpp
    src/commands.cpp
    src/command_builder.cpp
    src/fleet_access.cpp
    src/army_access.cpp
    src/event_manager.cpp
    src/notification_manager.cpp
    src/alert_manager.cpp
    src/tech_manager.cpp
    src/situation_log_manager.cpp
    src/galaxy_manager.cpp
    src/government_manager.cpp
    src/society_manager.cpp
    src/leader_manager.cpp
    src/species_manager.cpp
    src/fleet_manager.cpp
    src/ship_designer.cpp
    src/market_manager.cpp
    src/discoveries_manager.cpp
    src/contacts_manager.cpp
    src/outliner_manager.cpp
)

target_include_directories(stellaris_bridge PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(stellaris_bridge PRIVATE
    minhook
    nlohmann_json::nlohmann_json
    d3d11
    dxgi
)

target_compile_definitions(stellaris_bridge PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)

if (MSVC)
    target_compile_options(stellaris_bridge PRIVATE /utf-8)
endif()

set_target_properties(stellaris_bridge PROPERTIES
    OUTPUT_NAME "stellaris_bridge"
    PREFIX ""
)
