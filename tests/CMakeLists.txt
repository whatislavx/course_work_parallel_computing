add_executable(search_tests
    smoke_test.cpp
    config_test.cpp
    http_test.cpp
    tokenizer_test.cpp
    document_source_test.cpp
    inverted_index_test.cpp
    thread_pool_test.cpp
    shared_inverted_index_test.cpp
    index_builder_test.cpp
    index_manager_test.cpp
    index_endpoint_test.cpp
    ranking_test.cpp
    snippet_test.cpp
    search_endpoint_test.cpp
    generate_test.cpp
    ollama_test.cpp
    expansion_fallback_test.cpp
    ws_search_test.cpp
    cli_test.cpp
)
target_link_libraries(search_tests PRIVATE search_core search_cli_core GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(search_tests PROPERTIES TIMEOUT 120)
