#include <clopts.hh>

using namespace command_line_options;

using o1 = clopts<multiple<multiple<option<"--bar", "Bar">>>>; // expected-error@clopts.hh:* {{multiple<multiple<>> is invalid}}
using o2 = clopts<multiple<stop_parsing<>>>; // expected-error@clopts.hh:* {{multiple<stop_parsing<>> is invalid}}

// expected-note@* + {{instantiation}}
