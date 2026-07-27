#include <contract/tools/ModValidator.hpp>

#include <iostream>
#include <string>
#include <vector>

int main(int argc, char* argv[]) {
    std::vector<std::string> arguments;
    arguments.reserve(static_cast<std::size_t>(argc > 0 ? argc - 1 : 0));
    for (int index = 1; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }

    const auto options = contract::tools::parse_mod_validator_options(
        arguments,
        std::cerr);
    if (!options.has_value()) {
        contract::tools::print_mod_validator_help(std::cerr);
        return static_cast<int>(
            contract::tools::ModValidatorExitCode::usage_error);
    }
    if (options->help) {
        contract::tools::print_mod_validator_help(std::cout);
        return 0;
    }
    return contract::tools::run_mod_validator(
        *options,
        std::cout,
        std::cerr);
}
