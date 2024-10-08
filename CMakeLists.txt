# 顶层 CMakeLists.txt

cmake_minimum_required(VERSION 3.10)
project(sylar-from-suycx)

include(cmake/utils.cmake)

set(CMAKE_VERBOSE_MAKEFILE ON)

# 指定编译选项
set(CMAKE_CXX_FLAGS "$ENV{CXXFLAGS} -std=c++11 -O0 -ggdb -Wall -Werror")

# -rdynamic: 将所有符号都加入到符号表中，便于使用dlopen或者backtrace追踪到符号
# -fPIC: 生成位置无关的代码，便于动态链接
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -rdynamic -fPIC")

# -Wno-unused-function: 不要警告未使用函数
# -Wno-builtin-macro-redefined: 不要警告内置宏重定义，用于重定义内置的__FILE__宏
# -Wno-deprecated: 不要警告过时的特性
# -Wno-deprecated-declarations: 不要警告使用带deprecated属性的变量，类型，函数
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-function -Wno-builtin-macro-redefined -Wno-deprecated -Wno-deprecated-declarations")

include_directories(.)
include_directories(/usr/local/include)

option(BUILD_TEST "ON for complile test" ON)

# find_package(Boost REQUIRED) 
# if(Boost_FOUND)
#     include_directories(${Boost_INCLUDE_DIRS})
# endif()

# 源文件列表
set(LIB_SRC
    sylar/log.cpp
    sylar/util.cpp
    sylar/mutex.cc
    sylar/env.cc
    sylar/config.cc
    sylar/thread.cc
    sylar/fiber.cc
    sylar/scheduler.cc
    sylar/iomanager.cc
    sylar/timer.cc
    sylar/fd_manager.cc
    sylar/hook.cc
    sylar/address.cc 
    sylar/socket.cc 
    sylar/bytearray.cc 
    sylar/tcp_server.cc 
    sylar/http/http-parser/http_parser.c 
    sylar/http/http.cc
    sylar/http/http_parser.cc 
    sylar/stream.cc 
    sylar/streams/socket_stream.cc
    sylar/http/http_session.cc 
    sylar/http/servlet.cc
    sylar/http/http_server.cc 
    sylar/uri.cc 
    sylar/http/http_connection.cc 
    sylar/daemon.cc 

    sylar/rpc/rpcconfig.cc
    sylar/rpc/rpccontroller.cc
    sylar/rpc/rpcheader.pb.cc
    sylar/rpc/rpcchannel.cc
    sylar/rpc/rpcapplication.cc
    sylar/rpc/zookeeperutil.cc
    sylar/rpc/rpcprovider.cc
)

add_library(sylar SHARED ${LIB_SRC})
force_redefine_file_macro_for_sources(sylar)

set(LIBS
    sylar
    pthread
    dl
    yaml-cpp
    protobuf
    zookeeper_mt
)

# 仅使用上面 libsylar.so 的构建过程中，如果 yaml-cpp 等库没有明确地链接
# 链接器可能无法找到并解析 sylar 中所需的符号，导致使用 sylar库 链接阶段报出符号未定义的错误。
target_link_libraries(sylar PRIVATE yaml-cpp zookeeper_mt dl protobuf pthread)

if(BUILD_TEST)
    sylar_add_executable(test_log "tests/test_log.cpp" sylar "${LIBS}")
    sylar_add_executable(test_util "tests/test_util.cpp" sylar "${LIBS}")
    sylar_add_executable(test_env "tests/test_env.cc" sylar "${LIBS}")
    sylar_add_executable(test_config "tests/test_config.cc" sylar "${LIBS}")
    sylar_add_executable(test_thread "tests/test_thread.cc" sylar "${LIBS}")
    sylar_add_executable(test_fiber "tests/test_fiber.cc" sylar "${LIBS}")
    sylar_add_executable(test_fiber2 "tests/test_fiber2.cc" sylar "${LIBS}")
    # test_fiber_swap test_thread_swap
    sylar_add_executable(test_fiber_swap "tests/test_fiber_swap.cc" sylar "${LIBS}")
    sylar_add_executable(test_thread_swap "tests/test_thread_swap.cc" sylar "${LIBS}")
    sylar_add_executable(test_scheduler "tests/test_scheduler.cc" sylar "${LIBS}")
    sylar_add_executable(test_iomanager "tests/test_iomanager.cc" sylar "${LIBS}")
    sylar_add_executable(test_timer "tests/test_timer.cc" sylar "${LIBS}")
    sylar_add_executable(test_hook "tests/test_hook.cc" sylar "${LIBS}")
    sylar_add_executable(test_address "tests/test_address.cc" sylar "${LIBS}")
    sylar_add_executable(test_socket_tcp_server "tests/test_socket_tcp_server.cc" sylar "${LIBS}")
    sylar_add_executable(test_socket_tcp_client "tests/test_socket_tcp_client.cc" sylar "${LIBS}")
    sylar_add_executable(test_bytearray "tests/test_bytearray.cc" sylar "${LIBS}")
    sylar_add_executable(test_tcp_server "tests/test_tcp_server.cc" sylar "${LIBS}")
    sylar_add_executable(test_http "tests/test_http.cc" sylar "${LIBS}")
    sylar_add_executable(test_http_parser "tests/test_http_parser.cc" sylar "${LIBS}")
    sylar_add_executable(test_http_server "tests/test_http_server.cc" sylar "${LIBS}")
    sylar_add_executable(test_tcpserver_passure "tests/test_tcpserver_passure.cc" sylar "${LIBS}")
    sylar_add_executable(test_tcpserver_bench "tests/test_tcpserver_bench.cc" sylar "${LIBS}")
    sylar_add_executable(test_uri "tests/test_uri.cc" sylar "${LIBS}")
    sylar_add_executable(test_http_connection "tests/test_http_connection.cc" sylar "${LIBS}")
    sylar_add_executable(test_daemon "tests/test_daemon.cc" sylar "${LIBS}")
endif()

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)

# 设置安装前缀
set(CMAKE_INSTALL_PREFIX /usr/local)

# 添加头文件安装规则
install(DIRECTORY sylar/
    DESTINATION include/sylar
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
)

# 添加库文件安装规则
install(TARGETS sylar
    LIBRARY DESTINATION lib
)

add_subdirectory(${PROJECT_SOURCE_DIR}/tests/rpc)
