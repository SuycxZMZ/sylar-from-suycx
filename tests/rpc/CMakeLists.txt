include_directories(${PROJECT_SOURCE_DIR}/sylar)
set(LISTCALLER calluserservice.cc user.pb.cc)
set(LISTPROVIDER userservice.cc user.pb.cc)

add_executable(caller ${LISTCALLER})
add_executable(provider ${LISTPROVIDER})
target_link_libraries(caller sylar pthread protobuf)
target_link_libraries(provider sylar pthread protobuf dl)