# Info
This is a type-safe, header-only compile-time **c**ommand **l**ine 
**opt**ion**s** library that generates a parser and a formatted help 
message at compile time. 

It also ensures that your program doesn't compile if you, for instance, 
misspell the name of an option when trying to access its value.

Amongst other things, both `--option value` and `--option=value` are supported.

## Build
You only need to `#include "include/clopts.hh"` and then you're good to go.

## Usage
### Example
The following is a simple self-contained program that shows how to use this 
library:
```c++
#include "/path/to/clopts.hh"
using namespace command_line_options;

int x = 42;
static void print_42_and_exit(void* arg) {
    int* i = reinterpret_cast<int*>(arg);
    std::cout << *i;
    std::exit(0);
}

using options = clopts< // clang-format off
    positional<"file", "The name of the file", file_data, true>,
    positional<"foobar", "Foobar description goes here", std::string, false>,
    option<"--size", "Size of something goes here", int64_t>,
    multiple<option<"--int", "Integers", int64_t, true>>,
    flag<"--frobnicate", "Whether to frobnicate">,
    func<"--func", "Print 42 and exit", print_42_and_exit, (void*) &x>,
    help
>; // clang-format on

int main(int argc, char** argv) {
    options::parse(argc, argv);
    if (auto *ints = options::get<"--int">()) {
        for (const auto& i : *ints) std::cout << i << "\n";
    } else {
        std::cout << "No ints!\n";
    }
}
```

For how errors are handled, see the ‘Error Handling’ section below.

### Defining Options
The `clopts` type is the top-level container for all of your options.
```c++
using options = clopts<
    option<"--size", "The size of the file", int64_t>
>;
```

Make sure you do *not* try and do this:
```c++
clopts<
    option<"--size", "The size of the file", int64_t>
> options;
```

The types defined by this library are *not* meant to be instantiated.

At runtime, the options are parsed by calling the `parse()` function of
your `clopts` type, passing it `argc` and `argv`.
```c++
options::parse(argc, argv);
```

For a detailed description of all the option types, see the `Option Types`
section below.

### Accessing Option Values
The `get<>()` function returns a pointer to an option value, or nullptr if
the option is not found:
```c++
options::parse(argc, argv);
if (auto* size = options::get<"--size">()) {
    /// do something
}
```

Note that if no option with the name `--size` exists, you'll get a 
compile-time error.

### Error Handling
This section only concerns errors that occur when parsing the options;
errors that would make the options unparseable or ill-formed, such as having
two different options with the same name, or trying to call `get<>()` on an
option that doesn't exist, are compile-time errors.

When an error is encountered, the parser invokes your `clopts` type's error
handler, which, by default, prints the error, the help message, and then
exits the program with code `1`.

You can change this behaviour by installing your own error handler:
```c++
options::handle_error = [&](std::string&& errmsg) {
    std::cerr << "PANIC!";
    return false;
};
```

The error handler must return a bool which tells the parser whether it
should continue the parsing process: `true` means continue, `false` means
abort (that is, the parsing process, not the entire program).

## Option types
This library comes with several builtin option types that are meant to
address the most common use cases. You can also define your own [custom option
types](#custom-option-types) by deriving from `option`, but more on that later.

Depending on the [type](#type), some options take arguments, others don't.

NOTE: Not all option types are currently documented. See the example above for
additional option types that may not be documented below.

Different types of options can be added by passing them to the `clopts` type
as template parameters.
```c++
using options = clopts<
    option<"--size", "The size of the file", int64_t>,
    positional<"foobar", "Description goes here">,
    flag<"--frobnicate", "Whether to frobnicate">,
>;        
```

The options themselves are also templates. What parameters they take depends
on the type of option, but in general every option must take at least two
parameters, both of which must be string literals: first, the name of the
option, which is how you will be accessing its value; and secondly, a
description of what the option is, which is used when generating the help
message.

### option
The most basic option type is `option`. It takes up to four parameters, the
last two of which are optional.
```c++
option<"--name", "Description">
option<"--name", "Description", int64_t>
option<"--name", "Description", int64_t, /* required? */ true>
```

#### Parameters
* The option name, which must be a string literal (at most 256 bytes).
* A description of the option, also a string literal (at most 512 bytes).
* The type of the option (`bool`, `std::string`, `file_data` `int64_t`, or 
  `void (*)(void*)`). The default is `std::string`.  
  The `file_data` type is defined in the `command_line_options` namespace;
  it is an empty struct, and its only purpose is to tell the parser to treat
  the argument of this option as a filename; the actual value of this option
  when accessed using `get<>()` is an `std::string` containing the contents 
  of the file.  
  If the parser can't load the file (for instance, because it 
  doesn't exist), it will invoke the error handler with an appropriate message,
  and the value of the option in the `parsed_options` struct will be left in an
  indeterminate state.
* Whether the option is required, i.e. whether omitting it is an error.
  The default is `false`.

#### Types and Arguments
The third parameter, the type, influences how the option is parsed. If the
type is `bool`, then the option is a [flag](#flag) (see below).

If the type is `void(*)(void*)`, then the option is a [func](#func) (see below).
**Never** use the `option` template for this; use the `func` template instead.

If the type is `std::string` or `int64_t`, then the option takes an argument.
In the case of `int64_t`, the argument must be a valid 64-bit integer.

Both `--option value` and `--option=value` are recognised by the parser.

### flag
A `flag` is equivalent to an `option` of type `bool`. Flags have no
argument: they're `true` when they're present, and `false` otherwise. For
flags, calling `has<>()` has the same effect as calling `get<>()`.
```c++
flag<"--name", "Description">
flag<"--name", "Description", /* required? */ true>
```

### func
A `func` is different from other options in that it doesn't have a value, 
but rather, it defines a callback that is called by the parser when the
option is encountered. You can specify additional data to be passed
to the callback in the form of a `void*`.
```c++
int x = 42;
static void print_42_and_exit(void* arg) {
    int* i = reinterpret_cast<int*>(arg);
    std::cout << *i;
    std::exit(0);
}

func<"--print42", "Print 42 and exit", print_42_and_exit, (void*) &x>,
```

It is a compile-time error to call `get<>()` on an option with function type.

### positional
Positional arguments are arguments that do not have a name on the command
line. If the parser encounters an option that it does not recognise, it
will store it in the first positional argument that does not yet have
a value.

Positional arguments are required by default.

By way of illustration, consider the following options:
```c++
clopts<
    flag<"--foo", "Description">,
    positional<"bar", "Description">,
    positional<"baz", "Description">
>
```

A program that uses these options can be called as follows:
```bash
./program opt1 opt2 --foo
./program opt1 --foo opt2
./program --foo opt1 opt2
# and so on
```

In the examples above, calling `get<"bar">()` always returns `"opt1"`, and
calling `get<"baz">()` always returns `"opt2"`.

### help
This is a special option type that takes no arguments and adds a `--help`
option that simply prints a help message containing the names, types, and
descriptions of all options and then exits the program.

This message can also be retrieved by calling the static `help()` function
of the `clopts` type.

### Custom Option Types
You can define custom option types by inheriting from `option`. For instance
the builtin `flag` type is defined as
```c++
template <static_string _name, static_string _description = "", bool required = false>
struct flag : public option<_name, _description, bool, required> {
    constexpr flag() = delete;
};
```

Make sure to use `static_string` if you want to be able to pass a string
literal as a non-type template parameter.

However, keep in mind that implementing certain features, like the `func` 
type, would also require modifying the parser. 
