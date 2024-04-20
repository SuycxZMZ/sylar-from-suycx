cmake_minimum_required(VERSION 3.0)

project(sylar-ucontext)

# add_definitions(-DMUDUODEBUG=1)
# add_definitions(-D_XOPEN_SOURCE=600)
# add_definitions(-D_GNU_SOURCE=1006)

include_directories(${PROJECT_SOURCE_DIR}/include)

set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -std=c++17 -fPIC")

add_subdirectory(include)

add_subdirectory(tests) 
# aux_source_directory(. SRC_LIST)

# add_library(tinymuduo SHARED ${SRC_LIST})