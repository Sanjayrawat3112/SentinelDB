cmake_minimum_required(VERSION 3.10)
project(SentinelDB_Phase1 CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra)

add_executable(test_pager pager.cpp main.cpp)
add_executable(run_tests pager.cpp tests.cpp)
add_executable(run_bp_tests pager.cpp buffer_pool.cpp test_buffer_pool.cpp)
# Storage engine (Teammate A)
add_executable(page_ops_tests storage_engine/page_ops.cpp storage_engine/test_page_ops.cpp)
target_include_directories(page_ops_tests PRIVATE ${CMAKE_SOURCE_DIR})

add_executable(kv_store_tests pager.cpp buffer_pool.cpp storage_engine/page_ops.cpp storage_engine/kv_store.cpp storage_engine/test_kv_store.cpp)
target_include_directories(kv_store_tests PRIVATE ${CMAKE_SOURCE_DIR})

add_executable(wal_tests storage_engine/wal.cpp storage_engine/test_wal.cpp)
target_include_directories(wal_tests PRIVATE ${CMAKE_SOURCE_DIR})
