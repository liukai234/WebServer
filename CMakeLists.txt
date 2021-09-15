cmake_minimum_required(VERSION 3.16)
project(myhttpd)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "-W -Wall ${CMAKE_C_FLAGS}")
link_libraries(pthread)

set(EXECUTABLE_OUTPUT_PATH "../")
add_executable(myhttpd httpd.c)
