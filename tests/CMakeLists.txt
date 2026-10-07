add_executable(unit_tests
    test_main.cpp
    test_coord_math.cpp
    test_shared_container.cpp
    test_game_clock.cpp
    test_config.cpp
    test_logger.cpp
    test_map_generator.cpp
)

# The helper headers under test are header-only; expose them directly.
# MapGenerator.cpp includes <pch.h>, so the pch directory is needed too.
target_include_directories(unit_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/helpers
    ${CMAKE_SOURCE_DIR}/src/pch
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
)

if(OpenGL_FOUND)
    target_link_libraries(unit_tests PRIVATE OpenGL::GL)
endif()

add_test(NAME unit_tests COMMAND unit_tests)
