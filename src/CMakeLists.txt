find_package(Boost 1.83 REQUIRED COMPONENTS json)
find_package(Threads REQUIRED)

# Бібліотека серверних модулів: конфігурація і HTTP-сервер з health (TCK-002, TCK-003).
add_library(search_core STATIC
    config/config.cpp
    server/http_server.cpp
    server/router.cpp
)
target_include_directories(search_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(search_core PUBLIC search_warnings Boost::json Threads::Threads)

add_executable(search_server main.cpp)
target_link_libraries(search_server PRIVATE search_core)
