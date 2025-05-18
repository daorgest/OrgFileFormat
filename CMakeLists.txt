cmake_minimum_required(VERSION 3.10)
project(orgfileformat)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(3rdParty/zstd/build/cmake)

add_executable(orgfileformat main.cpp)

target_link_libraries(orgfileformat PRIVATE libzstd_static)