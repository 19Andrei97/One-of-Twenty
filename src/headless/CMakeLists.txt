# Headless simulation: the game's clock/map/entity code driven with no window
# and no GL context. Kept out of Game/Scene so it never opens a window.
add_library(HeadlessSim
    HeadlessSimulation.cpp
    HeadlessSimulation.h
)

target_include_directories(HeadlessSim PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(HeadlessSim
    PUBLIC
    pch
    MapGenerator
    EntityManager
)

# The CLI front end, built by default. Disable with -DONE_OF_TWENTY_BUILD_SIM=OFF.
add_executable(OneOfTwentySim main_headless.cpp)
target_link_libraries(OneOfTwentySim PRIVATE HeadlessSim)
