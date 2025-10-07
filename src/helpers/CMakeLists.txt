# Header-only library
add_library(Helpers INTERFACE)

# Directories for linking to Helpers
target_include_directories(Helpers
    INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}
)

# Include libraries
target_link_libraries(Helpers INTERFACE
    spdlog::spdlog
    SFML::Graphics
    SFML::Window
    SFML::System
)
