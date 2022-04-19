#include "../include/clopts.hh"
using namespace command_line_options;

static void print_42_and_exit(void*) {
	std::cout << 42;
	std::exit(0);
}

/// TODO:
///  - multiple<option<"--filename", "The name of the file", std::string, true>> => std::vector<std::string>
///	 - alias<"-f", "--filename">

using options = clopts< // clang-format off
	option<"--filename", "The name of the file", std::string, true>,
	option<"--size", "The size of the file", int64_t>,
	positional<"foobar", "Foobar description goes here">,
	flag<"--frobnicate", "Whether to frobnicate">,
	func<"--lambda", "Print 42 and exit", print_42_and_exit>,
	help
>; // clang-format on

int main(int argc, char** argv) {
	auto opts = options::parse(argc, argv);
	std::cout << opts.get<"foobar">();
}