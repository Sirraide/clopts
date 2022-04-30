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
///	 - alias<"-f", "--filename">

using options = clopts< // clang-format off
	option<"--filename", "The name of the file", std::string, true>,
	option<"--size", "The size of the file", int64_t>,
	positional<"foobar", "Foobar description goes here">,
	flag<"--frobnicate", "Whether to frobnicate">,
	func<"--func", "Print 42 and exit", print_42_and_exit, (void*) &x>,
	help
>; // clang-format on

int main(int argc, char** argv) {
	options::handle_error = [&](std::string&& errmsg) {
		std::cerr << "PANIC: " << errmsg;
		return false;
	};

	auto opts = options::parse(argc, argv);
	if (opts.has<"--func">()) std::cout << opts.get<"--size">() << "\n";
}