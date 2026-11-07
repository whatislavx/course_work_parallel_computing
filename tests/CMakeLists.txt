add_executable(search_tests
    smoke_test.cpp
    config_test.cpp
    http_test.cpp
    tokenizer_test.cpp
    document_source_test.cpp
    inverted_index_test.cpp
    ollama_test.cpp
)
target_link_libraries(search_tests PRIVATE search_core GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(search_tests)
