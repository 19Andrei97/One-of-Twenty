cmake_minimum_required(VERSION 3.20)
project(OneOfTwenty LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

include(FetchContent)

if (MSVC)
    add_compile_options(/utf-8)
endif()

# Copying assets to build
file(COPY ${CMAKE_SOURCE_DIR}/fonts DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
file(COPY ${CMAKE_SOURCE_DIR}/config DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

######################################################

# SFML
find_package(SFML COMPONENTS System Graphics Window REQUIRED)

# Fetch the library if it's missing
if(NOT SFML_FOUND)
    FetchContent_Declare(SFML
        GIT_REPOSITORY https://github.com/SFML/SFML.git
        GIT_TAG 3.0.0
        GIT_SHALLOW ON
        EXCLUDE_FROM_ALL)

    FetchContent_MakeAvailable(SFML)
endif()

######################################################

# EnTT
find_package(EnTT CONFIG QUIET)

if(NOT EnTT_FOUND)
    message(STATUS "EnTT not found — fetching from GitHub...")

    FetchContent_Declare(
        EnTT
        GIT_REPOSITORY https://github.com/skypjack/entt.git
        GIT_SHALLOW ON
    )

    FetchContent_MakeAvailable(EnTT)
else()
    message(STATUS "EnTT found in ${EnTT_DIR}")
endif()

######################################################

# Header-only BS_thread_pool
add_library(BS_thread_pool INTERFACE)

if(WIN32)
    # Search for BS_thread_pool
    find_path(BS_THREAD_POOL_INCLUDE_DIR
        NAMES BS_thread_pool.hpp
        PATHS ${VCPKG_INSTALLED_DIR}/x64-windows/include
    )
endif()

# Check if found BS_thread_pool
if(BS_THREAD_POOL_INCLUDE_DIR)
    message(STATUS "Found BS_thread_pool at: ${BS_THREAD_POOL_INCLUDE_DIR}")
    target_include_directories(BS_thread_pool INTERFACE ${BS_THREAD_POOL_INCLUDE_DIR})
else()
    message(FATAL_ERROR "BS_thread_pool.hpp not found — installation is required check the README file for more information on the library.")
endif()

######################################################

# spdlog
find_package(spdlog REQUIRED)

# Fetch the library if it's missing
if(NOT spdlog_FOUND)
    message(STATUS "spdlog not found — fetching from GitHub...")

    FetchContent_Declare(spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_SHALLOW ON
        EXCLUDE_FROM_ALL)

    FetchContent_MakeAvailable(spdlog)
else()
    message(STATUS "Library spdlog was found in ${spdlog_DIR}")
endif()

######################################################

# nlohmann_json
find_package(nlohmann_json REQUIRED)

# Fetch the library if it's missing
if(NOT nlohmann_json_FOUND)
    message(STATUS "nlohmann_json not found — fetching from GitHub...")

    FetchContent_Declare(nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_SHALLOW ON
        EXCLUDE_FROM_ALL)

    FetchContent_MakeAvailable(nlohmann_json)
else()
    message(STATUS "Library nlohmann_json was found in ${nlohmann_json_DIR}")
endif()

######################################################

# Add submodules
add_subdirectory(src/pch)
add_subdirectory(src/camera)
add_subdirectory(src/entity_manager)
add_subdirectory(src/game)
add_subdirectory(src/components)
add_subdirectory(src/helpers)
add_subdirectory(src/hud)
add_subdirectory(src/map_generator)

# Main executable
add_executable(${PROJECT_NAME} src/main.cpp)

# Link internal libraries
target_link_libraries(${PROJECT_NAME}
    PRIVATE
        pch
        Camera
        EntityManager
        Game
        Components
        Helpers
        Hud
        MapGenerator
        BS_thread_pool
        nlohmann_json::nlohmann_json
        spdlog::spdlog
        SFML::System
        SFML::Window
        SFML::Graphics
        EnTT::EnTT
)