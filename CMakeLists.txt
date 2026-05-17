cmake_minimum_required(VERSION 3.16)
project(red_black_knights LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(red_black_knights
    src/main.cpp
)

target_compile_options(red_black_knights PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive->
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
)

target_link_libraries(red_black_knights PRIVATE windowscodecs ole32)