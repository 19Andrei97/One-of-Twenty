# Header-only library
add_library(Components INTERFACE)

# Directories for linking
target_include_directories(Components
    INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}
)

# Include libraries
target_link_libraries(Components INTERFACE
    spdlog::spdlog
    SFML::Graphics
    SFML::Window
    SFML::System
)
