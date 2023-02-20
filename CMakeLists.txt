cmake_minimum_required(VERSION 3.0.0)
project(WebServer VERSION 0.1.0)

include(CTest)
enable_testing()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

aux_source_directory(. C_SUC)
aux_source_directory(./CGImysql CGImysql_SUC)
aux_source_directory(./http http_SUC)
aux_source_directory(./timer timer_SUC)


link_directories(/usr/lib/usr/lib/x86_64-linux-gnu)
link_libraries(libmysqlclient.so)
add_executable(WebServer ${C_SUC} ${CGImysql_SUC} ${http_SUC} ${timer_SUC})

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
