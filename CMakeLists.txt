cmake_minimum_required(VERSION 3.0)

project(sylar-ucontext)

# add_definitions(-DMUDUODEBUG=1)

include_directories(${PROJECT_SOURCE_DIR}/include)

set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -std=c++17 -fPIC")

add_subdirectory(include)

# aux_source_directory(. SRC_LIST)

# add_library(tinymuduo SHARED ${SRC_LIST})