cmake_minimum_required(VERSION 3.0.0)

project(app C)

set(CMAKE_VERBOSE_MAKEFILE OFF)
set(CMAKE_C_FLAGS " -g -Wall -std=gnu99 ")
include_directories(sure udt)
add_subdirectory(sure)
add_subdirectory(udt)

# executable 
add_executable(copy_file copy_file.c)
add_executable(receive_file receive_file.c)

# library linking 
target_link_libraries(copy_file PUBLIC sure udt pthread)
target_link_libraries(receive_file PUBLIC sure udt pthread)
