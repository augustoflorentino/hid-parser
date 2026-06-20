cmake_minimum_required(VERSION 3.16)
project(hid-parser_consumer LANGUAGES C)

find_package(hid-parser REQUIRED)

add_executable(consumer ${CMAKE_CURRENT_SOURCE_DIR}/../../examples/hiddump.c)
target_link_libraries(consumer PRIVATE hid-parser::hid-parser)
