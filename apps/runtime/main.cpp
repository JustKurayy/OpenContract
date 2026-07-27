#include <contract/runtime/Runtime.hpp>

#include <iostream>
#include <string>
#include <vector>

int main(int argc, char* argv[]) {
    std::vector<std::string> arguments;
    arguments.reserve(static_cast<std::size_t>(argc > 0 ? argc - 1 : 0));
    for (int index = 1; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }

    const auto options = contract::runtime::parse_runtime_options(
        arguments,
        std::cerr);
    if (!options.has_value()) {
        return static_cast<int>(contract::runtime::RuntimeExitCode::usage_error);
    }
    if (options->help) {
        contract::runtime::print_runtime_help(std::cout);
        return static_cast<int>(contract::runtime::RuntimeExitCode::success);
    }
    return contract::runtime::run_runtime(*options, std::cout, std::cerr);
}
