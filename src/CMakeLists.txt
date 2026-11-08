find_package(Boost 1.83 REQUIRED COMPONENTS json)
find_package(Threads REQUIRED)

# Бібліотека серверних модулів: конфігурація, HTTP-сервер, перевірка Ollama (TCK-002, 003, 005).
add_library(search_core STATIC
    config/config.cpp
    index/tokenizer.cpp
    index/document_source.cpp
    index/term_table.cpp
    index/inverted_index.cpp
    index/shared_inverted_index.cpp
    index/index_builder.cpp
    index/index_manager.cpp
    pool/task_queue.cpp
    pool/thread_pool.cpp
    server/http_server.cpp
    server/router.cpp
    search/ranking.cpp
    ollama/endpoint.cpp
    ollama/probe.cpp
)
target_include_directories(search_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(search_core PUBLIC search_warnings Boost::json Threads::Threads)

add_executable(search_server main.cpp)
target_link_libraries(search_server PRIVATE search_core)
