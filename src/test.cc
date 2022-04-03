#include "../include/clopts.hh"

static void print_42_and_exit() {
	std::cout << 42;
	std::exit(0);
}

using options = clopts< // clang-format off
	option<"--filename", "The name of the file", std::string, true>,
	option<"--frobnicate", "Whether to frobnicate", bool>,
	option<"--size", "The size of the file", int64_t>,
	func<"--lambda", "Print 42 and exit", print_42_and_exit>
>; // clang-format on

int main(int argc, char** argv) {
	auto opts = options::parse(argc, argv);
	std::cout << opts.get<"--filename">();
}