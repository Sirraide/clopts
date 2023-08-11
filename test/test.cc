#include "../include/clopts.hh"
using namespace command_line_options;

static void print_number_and_exit(void* arg, std::string_view) {
    int* i = reinterpret_cast<int*>(arg);
    std::cout << *i;
    std::exit(0);
}

/// TODO:
///  - alias<"-f", "--filename">

static void custom_help(std::string_view msg) {
    std::cerr << "./test " << msg << "\n";
    std::cerr << "Additional help information goes here.\n";
    std::exit(1);
}

using options = clopts< // clang-format off
    positional<"file", "The name of the file", file<>, false>,
    positional<"foobar", "Foobar description goes here", std::string, false>,
    option<"--size", "Size of something goes here", int64_t>,
    multiple<option<"--int", "Integers", int64_t, true>>,
    option<"--lang", "Language option", values<1, 27, 42, 58>>,
    flag<"--frobnicate", "Whether to frobnicate">,
    func<"--func", "Print 42 and exit", print_number_and_exit>,
    help<custom_help>
>; // clang-format on

int main(int argc, char** argv) {
    int number = 42;
    options::parse(argc, argv, &number);
    auto ints = options::get<"--int">();

    if (ints->empty()) std::cout << "No ints!\n";
    else for (const auto& i : *ints) std::cout << i << "\n";

    if (auto f = options::get<"file">()) {
        std::cout << f->contents << "\n";
    }

    if (auto lang = options::get<"--lang">()) {
        std::cout << "Language: " << *lang << "\n";
    }
}