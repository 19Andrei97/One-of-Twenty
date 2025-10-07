add_library(MapGenerator MapGenerator.cpp MapGenerator.h)

target_include_directories(MapGenerator PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(MapGenerator 
    PUBLIC     
    pch
    Helpers
    opengl32
)
