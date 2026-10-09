add_executable(unit_tests
    test_main.cpp
    test_coord_math.cpp
    test_shared_container.cpp
    test_game_clock.cpp
    test_config.cpp
    test_logger.cpp
    test_map_generator.cpp
    test_terrain.cpp
    test_entity_decision.cpp
    test_entity_vitals.cpp
    test_entity_pathing.cpp
    test_move_cost.cpp
    test_buildings.cpp
    test_economy.cpp
    test_observability.cpp
    test_appearance.cpp
    test_pathfinding.cpp
    test_hud.cpp
    test_scene.cpp
)

# The helper headers under test are header-only; expose them directly.
# MapGenerator.cpp includes <pch.h>, so the pch directory is needed too.
target_include_directories(unit_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/helpers
    ${CMAKE_SOURCE_DIR}/src/pch
    ${CMAKE_SOURCE_DIR}/src/entity_manager
    ${CMAKE_SOURCE_DIR}/src/hud
    ${CMAKE_SOURCE_DIR}/src/game
    ${CMAKE_SOURCE_DIR}/src/camera
    ${CMAKE_SOURCE_DIR}/src/components
)

# Tests read config/map_data.json; ctest runs from the build tree, so bake in
# the source directory rather than depending on the working directory.
target_compile_definitions(unit_tests PRIVATE
    ONE_OF_TWENTY_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

target_link_libraries(unit_tests PRIVATE
    doctest::doctest
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    SFML::System
    SFML::Graphics
    SFML::Window
    MapGenerator
    EntityManager
    Hud
    Game
)

if(OpenGL_FOUND)
    target_link_libraries(unit_tests PRIVATE OpenGL::GL)
endif()

add_test(NAME unit_tests COMMAND unit_tests)
