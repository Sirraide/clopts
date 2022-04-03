#include "../include/clopts.hh"

using options = clopts< // clang-format off
	option<"--filename", "The name of the file", std::string, true>,
	option<"--frobnicate", "Whether to frobnicate", bool>,
	option<"--size", "The size of the file", int64_t>
>; // clang-format on

int main(int argc, char** argv) {
	auto opts = options::parse(argc, argv);
	std::cout << opts.get<"--filename">();
}