add_executable(minidb_bench benchmark_main.cpp)
target_link_libraries(minidb_bench PRIVATE minidb_lib Threads::Threads)
