# aux_source_directory(. SRC_LIST)
# add_executable(echoserver ${SRC_LIST})

add_executable(sylar-echoserver echoserver.cc)
add_executable(raw-echoserver raw-echoserver.cc)
target_link_libraries(sylar-echoserver /home/codeCpp/sylar-from-suycx/lib/libsylar-ucontext.so)