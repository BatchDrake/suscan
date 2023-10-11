cmake_minimum_required(VERSION 3.20.0)
project(suscanTestPrj)

find_package(suscan REQUIRED)

add_executable(suscantest test.c)

target_link_libraries(suscantest suscan::suscan)