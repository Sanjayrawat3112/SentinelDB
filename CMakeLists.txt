cmake_minimum_required(VERSION 3.10)
project(SentinelDB_Phase1 CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra)

add_executable(test_pager pager.cpp main.cpp)
add_executable(run_tests pager.cpp tests.cpp)
