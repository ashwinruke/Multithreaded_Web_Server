cmake_minimum_required(VERSION 3.25)
project(multithreaded-server CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O2")

option(BUILD_TESTS "Build unit tests" ON)

find_package(Threads REQUIRED)

# Everything except main() lives in a library so the tests can link it.
add_library(server_core STATIC
    src/http.cpp
    src/router.cpp
    src/handlers.cpp
    src/net.cpp
    src/connection_handler.cpp
    src/pool_loop.cpp
    src/reactor_loop.cpp
    src/file_manager.cpp
    src/lru_cache.cpp
    src/logger.cpp
    src/utils.cpp
    src/metrics.cpp
    src/api.cpp
)
target_include_directories(server_core PUBLIC include)
target_link_libraries(server_core PUBLIC Threads::Threads)

add_executable(multithreaded-server src/main.cpp)
target_link_libraries(multithreaded-server PRIVATE server_core)

if(BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.15.2
        GIT_SHALLOW TRUE
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()
    add_executable(unit_tests
        tests/test_parser.cpp
        tests/test_router.cpp
        tests/test_cache.cpp
        tests/test_api.cpp
        tests/test_static.cpp
        tests/test_metrics.cpp
    )
    target_link_libraries(unit_tests PRIVATE server_core GTest::gtest_main)

    include(GoogleTest)
    gtest_discover_tests(unit_tests)
endif()
