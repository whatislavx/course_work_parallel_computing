// Консольний клієнт пошуку (TCK-025): ./search [--url URL] "запит".
#include "cli/search_cli.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char **argv) {
    search::cli::CliOptions options;
    if (const char *env = std::getenv("SEARCH_SERVER_URL"); env != nullptr && *env != '\0') {
        options.server_url = env;
    }
    std::vector<std::string> args(argv + 1, argv + argc);
    const std::string usage_error = search::cli::parse_arguments(args, &options);
    if (options.help) {
        std::cout << "usage: search [--url URL] \"query\"\n"
                     "  --url URL   search server (default http://127.0.0.1:8080, or "
                     "SEARCH_SERVER_URL)\n";
        return 0;
    }
    if (!usage_error.empty()) {
        std::cerr << "error: " << usage_error << '\n' << "usage: search [--url URL] \"query\"\n";
        return 2;
    }
    return search::cli::run_search(options, std::cout, std::cerr);
}
