aux_source_directory(. SRC_LIST)

add_library(sylar-ucontext SHARED ${SRC_LIST})

target_link_libraries(sylar-ucontext pthread)