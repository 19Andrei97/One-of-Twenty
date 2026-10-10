cmake_minimum_required(VERSION 3.20)
project(OneOfTwenty LANGUAGES CXX)

# CMake 4 removed compatibility with cmake_minimum_required(VERSION <3.5).
# SFML 3.0.0 bundles FreeType 2.13.2, whose CMakeLists still declares 3.0, so
# configuring fails while fetching SFML on systems without a pre-installed
# FreeType (notably Windows). This shim is read by CMake >= 4 only; older
# versions ignore it. Drop it once SFML is bumped to a release that bundles a
# newer FreeType (SFML 3.1.0 pins 2.14.3).
if(CMAKE_VERSION VERSION_GREATER_EQUAL 4.0)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

include(FetchContent)

# AddressSanitizer + UndefinedBehaviorSanitizer (GCC/Clang only). The flags are
# applied just before our own targets below, so fetched dependencies such as
# SFML are not rebuilt with the sanitizers.
option(ONE_OF_TWENTY_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

if (MSVC)
    add_compile_options(/utf-8)
endif()

# Copying assets to build
file(COPY ${CMAKE_SOURCE_DIR}/fonts DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
file(COPY ${CMAKE_SOURCE_DIR}/config DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

######################################################

# SFML
find_package(SFML COMPONENTS System Graphics Window QUIET)

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
        GIT_TAG v3.16.0
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
    # Header-only library with no CMakeLists of its own, so the fetched
    # include/ directory is exposed through our own INTERFACE target.
    message(STATUS "BS_thread_pool not found - fetching from GitHub...")

    FetchContent_Declare(bs_thread_pool
        GIT_REPOSITORY https://github.com/bshoshany/thread-pool.git
        GIT_TAG v5.1.0
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(bs_thread_pool)

    target_include_directories(BS_thread_pool INTERFACE ${bs_thread_pool_SOURCE_DIR}/include)
endif()

######################################################

# spdlog
find_package(spdlog QUIET)

# Fetch the library if it's missing
if(NOT spdlog_FOUND)
    message(STATUS "spdlog not found — fetching from GitHub...")

    FetchContent_Declare(spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG v1.17.0
        GIT_SHALLOW ON
        EXCLUDE_FROM_ALL)

    FetchContent_MakeAvailable(spdlog)
else()
    message(STATUS "Library spdlog was found in ${spdlog_DIR}")
endif()

######################################################

# nlohmann_json
find_package(nlohmann_json QUIET)

# Fetch the library if it's missing
if(NOT nlohmann_json_FOUND)
    message(STATUS "nlohmann_json not found — fetching from GitHub...")

    FetchContent_Declare(nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.12.0
        GIT_SHALLOW ON
        EXCLUDE_FROM_ALL)

    FetchContent_MakeAvailable(nlohmann_json)
else()
    message(STATUS "Library nlohmann_json was found in ${nlohmann_json_DIR}")
endif()

######################################################

# doctest (testing framework, header-only)
find_package(doctest QUIET)

if(NOT doctest_FOUND)
    FetchContent_Declare(doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG v2.5.3
        GIT_SHALLOW ON
        EXCLUDE_FROM_ALL)

    FetchContent_MakeAvailable(doctest)
endif()

######################################################

if(ONE_OF_TWENTY_SANITIZE)
    if(MSVC)
        message(FATAL_ERROR "ONE_OF_TWENTY_SANITIZE is only supported with GCC/Clang")
    endif()
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined)
endif()
# Add submodules
add_subdirectory(src/pch)
add_subdirectory(src/camera)
add_subdirectory(src/entity_manager)
add_subdirectory(src/game)
add_subdirectory(src/components)
add_subdirectory(src/helpers)
add_subdirectory(src/hud)
add_subdirectory(src/map_generator)

# Headless simulation runner (no window/GL). On by default; it makes the
# simulation runnable and testable without a screen.
option(ONE_OF_TWENTY_BUILD_SIM "Build the headless simulation runner" ON)
if(ONE_OF_TWENTY_BUILD_SIM)
    add_subdirectory(src/headless)
endif()

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

# Tests
option(ONE_OF_TWENTY_BUILD_TESTS "Build unit tests" ON)
if(ONE_OF_TWENTY_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
