# aux_source_directory(. SRC_LIST)
# add_executable(echoserver ${SRC_LIST})

add_executable(sylar-echoserver echoserver.cc)
add_executable(raw-echoserver raw-echoserver.cc)
add_executable(muduo-echoserver muduo-echoserver.cc)
target_link_libraries(muduo-echoserver tinymuduo pthread)
target_link_libraries(sylar-echoserver /home/suycx/code/sylar-from-suycx/lib/libsylar-ucontext.so pthread)