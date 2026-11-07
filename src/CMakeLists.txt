# Каркас серверного застосунку (TCK-001). Модулі додаються в наступних тікетах.
add_executable(search_server main.cpp)
target_link_libraries(search_server PRIVATE search_warnings)
