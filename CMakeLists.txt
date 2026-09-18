cmake_minimum_required(VERSION 3.25)
project(multithreaded-server CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O2")

find_package(Threads REQUIRED)

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
)
target_include_directories(server_core PUBLIC include)
target_link_libraries(server_core PUBLIC Threads::Threads)

add_executable(multithreaded-server src/main.cpp)
target_link_libraries(multithreaded-server PRIVATE server_core)
