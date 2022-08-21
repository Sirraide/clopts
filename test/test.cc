#include "../include/clopts.hh"
using namespace command_line_options;

int x = 42;
static void print_42_and_exit(void* arg) {
    int* i = reinterpret_cast<int*>(arg);
    std::cout << *i;
    std::exit(0);
}

/// TODO:
///  - multiple<option<"--filename", "The name of the file", std::string, true>> => std::vector<std::string>
///  - alias<"-f", "--filename">
///  - include a description of positional arguments in the help message

using options = clopts< // clang-format off
    positional<"file", "The name of the file", file_data, true>,
    positional<"foobar", "Foobar description goes here", std::string, false>,
    option<"--size", "Size of something goes here", int64_t>,
    multiple<option<"--int", "The name of the file", std::string, true>>,
    flag<"--frobnicate", "Whether to frobnicate">,
    func<"--func", "Print 42 and exit", print_42_and_exit, (void*) &x>,
    help
>; // clang-format on

int main(int argc, char** argv) {
    std::set_terminate(__gnu_cxx::__verbose_terminate_handler);

    auto opts = options::parse(argc, argv);
    auto ints = opts.get<"--int">();
    for (const auto& i : ints) std::cout << i << std::endl;
}