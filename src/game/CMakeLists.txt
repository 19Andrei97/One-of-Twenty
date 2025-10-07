add_library(Game Game.cpp Game.h)

target_include_directories(Game PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(Game 
    PUBLIC     
    pch
)