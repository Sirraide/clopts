# Info
This is a type-safe, header-only compile-time **c**ommand **l**ine 
**opt**ion**s** library that generates a parser and a formatted help 
message at compile time. 

It also ensures that your program doesn't compile if you, for instance, 
misspell the name of an option when trying to access its value.

Amongst other things, both `--option value` and `--option=value` are supported.

Everything in this library is defined in the `command_line_options` namespace,
which may be rather long, but it’s intended to be used in conjunction with either `using namespace ` or a namespace alias (e.g. `namespace cmd = command_line_options;`).

## Build
You only need to `#include <clopts.hh>` and then you're good to go.

Note: when this library is added as a CMake subdirectory, the path to `clopts.hh` is automatically added to the include path.

## Usage
### Example
The following is a simple self-contained program that shows how to use this 
library:
```c++
#include <clopts.hh>
using namespace command_line_options;

int x = 42;
static void print_42_and_exit(void* arg, std::string_view, std::string_view) {
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
    help<>
>; // clang-format on

int main(int argc, char** argv) {
    options::parse(argc, argv);
    if (auto* ints = options::get<"--int">()) {
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
your `clopts` type, passing it `argc` and `argv`. Note: At the moment, this means that option values are global and that options can only be parsed once. I have yet to encounter a use for parsing command line options multiple times during the runtime of a single program, but support for that may be added in a future release.
```c++
options::parse(argc, argv);
```

For a detailed description of all the option types, see the `Option Types`
section below.

### Accessing Option Values
The `get<>()` function returns a pointer to an option value, or nullptr if
the option is not found. If the option is a `flag` (see below), it instead returns a `true` if the option was found, and `false` if it wasn’t.
```c++
options::parse(argc, argv);
if (auto* size = options::get<"--size">()) {
    /// Do something.
}
```

Note that if no option with the name `--size` exists, you'll get a 
compile-time error.

Alternatively, the `get_or<>(value)` function can be used to get either the option value or a default value
if the option wasn’t found. Note that this function creates a copy of the option value and may thus incur extra
overhead if the option value happens to be a large string. If `value` is returned, it is first `static_cast` to
the option type.
```c++
options::parse(argc, argv);

/// Default size is 10.
std::cout << "Size: " << options::get_or<"--size">(10) << "\n";
```

### Error Handling
This section only concerns errors that occur when parsing the options;
errors that would make the options unparseable or ill-formed, such as having
two different options with the same name, or trying to call `get<>()` on an
option that doesn't exist, are compile-time errors.

When an error is encountered (such as an unrecognised option name or if an option that is marked as required was not specified), the parser invokes your `clopts` type's error
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
on the type, but in general every option must take at least two
parameters, both of which must be string literals (or, more generally, constant expressions of type `char[N]`): first, the name of the
option, which is how you will be accessing its value; and secondly, a
description of what the option is, which is used when generating the help
message.

Note that the name can be anything you want. The `--` are not required.

### Option Type: `option`
The most basic option type is `option`. This option type takes up to four template parameters, the last two of which are optional.
```c++
option<"--name", "Description">
option<"--name", "Description", std::string>
option<"--name", "Description", std::string, /* required? */ false>
```

#### **Parameters**
1. The option name, which must be a string literal (at most 256 bytes).
2. A description of the option, also a string literal (at most 512 bytes).
3. The type of the option (`std::string`, `file_data`, or `int64_t`). The default is `std::string`.
4. Whether the option is required, i.e. whether omitting it is an error.
  The default is `false`.

#### **Types and Arguments**
The `option` type always takes an argument. (Note: strictly speaking, this is not exactly true as `option` is the base class of most other option types, but users of the library should only use `option<>` with the data types listed above).

In the case of `int64_t`, the argument must be a valid 64-bit integer.

The `file_data` type indicates that the argument should be treated as a path to a file, the contents of which will be loaded into memory. When accessed with `get<>()`, both the path and contents will be returned. If the parser can't load the file (for instance, because it doesn't exist), it will invoke the error handler with an appropriate message, and the option value is left in an indeterminate state.

Both `--option value` and `--option=value` are recognised by the parser.

### Option Type: `flag`
Flags have no argument. For flags, `get<>()` returns a `bool` that is `true` when they're present, and `false` otherwise.
```c++
flag<"--name", "Description">
flag<"--name", "Description", /* required? */ false>
```

### Option Type: `positional`
Positional arguments are arguments that do not have a name on the command
line. If the parser encounters an option whose name it does not recognise, it
will store it in the first positional argument that does not yet have
a value. 

Just like `option`s, positional arguments take up to four template parameters and need not be strings. However, unlike `option`s, positional arguments are required by default.

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
```

In the examples above, `*get<"bar">()` is always `"opt1"`, and
`*get<"baz">()` is always `"opt2"`.

### Option Type: `help<>`
This is a special option type that takes no arguments and no template parameters and adds a `--help`
option that simply prints a help message containing the names, types, and
descriptions of all options and then exits the program. Note: You have to use `help<>`, rather than just `help`.

This message can also be retrieved by calling the static `help()` function
of the `clopts` type.

You can specify a custom help message handler to override the default behaviour by adding
it to the `help` type as a template parameter:
```c++
static void custom_help(void* msg, std::string_view, std::string_view) {
    std::cerr << *reinterpret_cast<std::string*>(msg) << "\n";
    std::cerr << "\nAdditional help information goes here.\n";
    std::exit(1);
}

clopts<
    help<custom_help>
>;
```

The complex signature required for such a function is rather unfortunate, but there will be no simple
of avoiding it until C++ allows `reinterpret_cast` to be used in constant expressions.

### Meta-Option Type: `multiple<>`
The `multiple` option type can not be used on its own and instead wraps another option and modifies it such that multiple occurences of that option are allowed:
```c++
multiple<option<"--int", "A number", int64_t>>
```
Calling `get<>()` on a `multiple<option<>>` or `multiple<positional<>>` returns a pointer to a `std::vector` of the option result type instead (e.g in the case of the `--int` option above, it will return a `std::vector<int64_t>*`).


#### **Properties**
* If the wrapped option is required, then it is required to be present at least once.
* `multiple<>` should only ever be used with `flag`s and `option`s.
* `multiple<func>` is currently invalid, as `func` options can already occur multiple times.
* There can only be at most one `multiple<positional<>>` option.
* `multiple<multiple<>>` is invalid.

### Option Type: `func`
A `func` defines a callback that is called by the parser when the
option is encountered. You can specify additional data to be passed
to the callback in the form of a `void*`. Furthermore, the parser
will pass the option name and value (if there is one) to the callback
as `std::string_view`s.

A `func` option can occur multiple times.

The following is an example of how to use the `func` option type:
```c++
int x = 42;
static void print_42_and_exit(void* arg, std::string_view, std::string_view) {
    int* i = reinterpret_cast<int*>(arg);
    std::cout << *i;
    std::exit(0);
}

func<"--print42", "Print 42 and exit", print_42_and_exit, (void*) &x>,
```

It is a compile-time error to call `get<>()` on a `func` option.

### Custom Option Types
You can define your own custom option types by inheriting from `option`. For instance the builtin `flag` type is defined as
```c++
template <static_string _name, static_string _description = "", bool required = false>
struct flag : public option<_name, _description, bool, required> {
    constexpr flag() = delete;
};
```

Make sure to use `static_string` if you want to be able to pass a string
literal as a non-type template parameter.

However, keep in mind that implementing certain features, like the `func` 
type, would also require modifying the main library code and that the library is not designed to allow for the addition of arbitrary new option types.
