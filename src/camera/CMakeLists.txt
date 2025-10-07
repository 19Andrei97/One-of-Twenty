add_library(Camera Camera.cpp Camera.h)

target_include_directories(Camera PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(Camera 
    PUBLIC     
    pch
    Helpers
)
