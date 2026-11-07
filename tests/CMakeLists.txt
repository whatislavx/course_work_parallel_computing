add_executable(search_tests smoke_test.cpp)
target_link_libraries(search_tests PRIVATE search_warnings GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(search_tests)
