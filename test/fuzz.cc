#include "../include/clopts.hh"
#include <ranges>

using namespace command_line_options;

static void nop() {
}

static void help_handler(std::string_view) {
    throw std::exception();
}

using options = clopts<
    positional<"foobar", "[description goes here]", std::string, false>,
    option<"--size", "The size parameter (whatever that means)", int64_t>,
    multiple<option<"--int", "Integers", int64_t, true>>,
    flag<"--test", "Test flag">,
    option<"--prime", "A prime number that is less than 14", values<2, 3, 5, 7, 11, 13>>,
    func<"--func", "foobar", nop>,
    help<help_handler>
>;

static bool error_handler(std::string&&) {
    throw std::exception();
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) try {
    // Split by spaces.
    std::vector<std::string> args_storage;
    for (auto arg : std::views::split(std::string_view(reinterpret_cast<const char*>(data), size), ' '))
        args_storage.emplace_back(arg.begin(), arg.end());

    std::vector<const char*> args;
    for (const auto& arg : args_storage)
        args.push_back(arg.c_str());

    options::parse(int(args.size()), args.data(), error_handler);
    return 0;
} catch (...) {
    return 0;
}
