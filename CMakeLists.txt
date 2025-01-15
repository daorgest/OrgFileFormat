cmake_minimum_required(VERSION 3.28)
project(OrgEngine_FilePack)

set(CMAKE_CXX_STANDARD 23)

add_executable(OrgEngine_FilePack main.cpp)
if (MSVC)
    target_compile_options(OrgEngine_FilePack PRIVATE
            $<$<CONFIG:Release>:/O2 /DNDEBUG /Ot>
    )
endif()