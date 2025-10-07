add_library(pch INTERFACE)

target_precompile_headers(pch INTERFACE
    "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/pch.h>"
)

target_include_directories(pch INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(pch INTERFACE
    Components
    Helpers
    EnTT::EnTT
    BS_thread_pool
    nlohmann_json::nlohmann_json
    SFML::System
    SFML::Window
    SFML::Graphics
)
