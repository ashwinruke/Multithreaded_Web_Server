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

add_executable(${PROJECT_NAME}
    src/main.cpp
    src/server.cpp
    src/http_parser.cpp
    src/http_response.cpp
    src/file_manager.cpp
    src/lru_cache.cpp
    src/logger.cpp
    src/utils.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE include)
target_link_libraries(${PROJECT_NAME} PRIVATE Threads::Threads)
