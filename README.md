# Info
This is a type-safe, header-only compile-time **c**ommand **l**ine 
**opt**ion**s** library that uses template metaprogramming to generate 
a parser and a formatted help message at compile time. 

It also ensures that your program doesn't compile if you, for instance, 
misspell the name of an option when trying to access its value.

Both `--option=value` and `--option=value` are supported.

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

using options = clopts<
    option<"--filename", "The name of the file", std::string, true>,
    option<"--size", "The size of the file", int64_t>,
    positional<"foobar", "Foobar description goes here">,
    flag<"--frobnicate", "Whether to frobnicate">,
    func<"--func", "Print 42 and exit", print_42_and_exit, (void*) &x>,
    help
>;

int main(int argc, char** argv) {
    options::handle_error = [&](std::string&& errmsg) {
        std::cerr << "PANIC: " << errmsg;
        return false;
    };
    
    auto opts = options::parse(argc, argv);
    if (opts.has<"--size">()) std::cout << opts.get<"--size">() << "\n";
}
```

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

Almost all the types defined by this library are *not* meant to be
instantiated. The only exception is `clopts::parsed_options`, which
you probably won't instantiate yourself. 

At runtime, the options are parsed by calling the `parse()` function of
your `clopts` type, passing it `argc` and `argv`. This function returns
a `parsed_options` struct that contains the values of the parsed options.
```c++
auto opts = options::parse(argc, argv);
```

For a detailed description of all the option types, see the `Option Types`
section below.

### Accessing Option Values
Before attempting to get the value of an option, you *must* first check 
whether it's present or not. This can be done using the `has<>()` function 
template:
```c++
auto opts = options::parse(argc, argv);
if (opts.has<"--size">()) {
    /// do something
}
```

Trying to call `opts.get<"--size">()` if `opts.has<"--size">()` returns
`false` may, but need not, throw a `std::bad_variant_access` exception.

Note that `opts.has<"--size">()` checks whether the `--size` option was 
encountered by the parser, i.e. whether the user specified it on the command
line. If instead no option with the name `--size` exists, you'll get a 
compile-time error instead.

You can then get the value of an option by passing the name of the option to
the `get<>` function template and calling it.
```c++
std::cout << opts.get<"foobar">();
```
**W A R N I N G:** The `get<>()` template is marked as `[[nodiscard]]`. *Make
sure not to ignore that!* At the time of writing, gcc versions before 11.3
have a [bug](https://gcc.gnu.org/bugzilla/show_bug.cgi?id=105143) that causes an internal compiler error when trying to emit a
`[[nodiscard]]` warning for `get<>()` if you don't use its return value.

Make sure to always use it or at least cast it to `(void)` if, for some reason,
you want to access, but not use it.

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
* The type of the option (`bool`, `std::string`, `int64_t`, or 
  `void (*)(void*)`). The default is `std::string`.
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

It is an error to call `get<>()` on an option with function type.

Unlike `flag`, it is not possible to use the `option` template directly to 
create a `func` option.

### positional
Positional arguments are arguments that do not have a name on the command
line. If the parser encounters an option that it does not recognise, it
will store it in the first positional argument that does not yet have
a value.

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
