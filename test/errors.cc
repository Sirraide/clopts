#include <clopts.hh>

using namespace command_line_options;

void callback(std::string_view) {}

using o1 = clopts<multiple<multiple<option<"--bar", "Bar">>>>; // expected-error@clopts.hh:* {{multiple<multiple<>> is invalid}}
using o2 = clopts<multiple<stop_parsing<>>>; // expected-error@clopts.hh:* {{multiple<stop_parsing<>> is invalid}}
using o3 = clopts<multiple<func<"foo", "bar", callback>>>; // expected-error@clopts.hh:* {{Type of multiple<> cannot be a callback}}
using o4 = clopts<multiple<help<>>>; // expected-error@clopts.hh:* {{Type of multiple<> cannot be a callback}}
using o5 = clopts<multiple<flag<"foo", "bar">>>; // expected-error@clopts.hh:* {{Type of multiple<> cannot be bool}}

int main(int argc, char** argv) {
    using o6 = clopts<multiple<positional<"foo", "bar">>, multiple<positional<"baz", "bar">>>;
    (void) o6::parse(argc, argv); // expected-error@clopts.hh:* {{Cannot have more than one multiple<positional<>> option}}

    using o7 = clopts<option<"foo", "bar">, flag<"foo", "baz">>;
    (void) o7::parse(argc, argv); // expected-error@clopts.hh:* {{Two different options may not have the same name}}
}

// expected-note@*           0+ {{}}
// expected-note@clopts.hh:* 0+ {{}}
