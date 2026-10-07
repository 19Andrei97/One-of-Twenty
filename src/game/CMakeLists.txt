add_library(Game 
    Game.cpp 
    Game.h
    Scene.cpp
    Scene.h
    Scene_Play.cpp
    Scene_Play.h
    Scene_Menu.cpp
    Scene_Menu.h
)

target_include_directories(Game PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(Game 
    PUBLIC     
    pch
    Camera
    EntityManager
    Hud
    MapGenerator
)
