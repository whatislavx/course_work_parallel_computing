# Модулі сервера (TCK-002). Далі додаються інші модулі.
add_library(search_core STATIC config/config.cpp)
target_include_directories(search_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(search_core PUBLIC search_warnings)

add_executable(search_server main.cpp)
target_link_libraries(search_server PRIVATE search_core)
