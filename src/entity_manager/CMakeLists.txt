add_library(EntityManager EntityManager.cpp EntityManager.h)

target_include_directories(EntityManager PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(EntityManager 
    PUBLIC     
    pch
    Helpers
)
