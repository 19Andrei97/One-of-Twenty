add_library(Hud Hud.cpp Hud.h)

target_include_directories(Hud PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(Hud 
    PUBLIC     
    pch
)
