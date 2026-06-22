add_executable(minidb_tests
    test_page.cpp
    test_buffer_pool.cpp
    test_btree.cpp
    test_parser.cpp
    test_executor.cpp
    test_txn.cpp
    test_recovery.cpp
    test_optimizer.cpp
    test_extension.cpp
    test_catalog.cpp
)
target_link_libraries(minidb_tests PRIVATE minidb_lib GTest::gtest_main Threads::Threads)
include(GoogleTest)
gtest_discover_tests(minidb_tests)
