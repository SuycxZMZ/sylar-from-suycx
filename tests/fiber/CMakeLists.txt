aux_source_directory(. SRC_LIST)
add_executable(simple-scheduler ${SRC_LIST})
target_link_libraries(simple-scheduler /home/codeCpp/sylar-from-suycx/lib/libsylar-ucontext.so)