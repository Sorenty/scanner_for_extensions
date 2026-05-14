cmake_minimum_required(VERSION 3.10)

project(scanner_for_extensions LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_executable(scanner_for_extensions
    src/main.cpp
    src/media_index.cpp
    src/scanner.cpp
    src/http_server.cpp
)

target_include_directories(scanner_for_extensions PRIVATE include)
target_link_libraries(scanner_for_extensions PRIVATE Threads::Threads)

target_compile_options(scanner_for_extensions PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)