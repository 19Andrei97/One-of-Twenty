add_library(MapGenerator
    MapGenerator.cpp
    MapGenerator.h
    generate_terrain.cpp
    generate_terrain.h
    Chunk.h
    MapConfig.h
)

target_include_directories(MapGenerator PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# OpenGL is only needed for the debug wireframe rendering
find_package(OpenGL QUIET)

target_link_libraries(MapGenerator
    PUBLIC
    pch
)

if(OpenGL_FOUND)
    target_link_libraries(MapGenerator PUBLIC OpenGL::GL)
endif()
