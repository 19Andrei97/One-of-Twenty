add_executable(unit_tests
    test_main.cpp
    test_coord_math.cpp
    test_shared_container.cpp
    test_game_clock.cpp
    test_config.cpp
)

# The helper headers under test are header-only; expose them directly.
target_include_directories(unit_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/helpers
)

target_link_libraries(unit_tests PRIVATE
    doctest::doctest
    nlohmann_json::nlohmann_json
    SFML::System
)

add_test(NAME unit_tests COMMAND unit_tests)
