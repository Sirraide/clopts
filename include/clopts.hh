#ifndef CLOPTS_H
#define CLOPTS_H

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iostream>
#include <string>

#ifndef CLOPTS_USE_MMAP
#    ifdef __linux__
#        define CLOPTS_USE_MMAP 1
#    else
#        define CLOPTS_USE_MMAP 0
#    endif
#endif

#if CLOPTS_USE_MMAP
#    include <fcntl.h>
#    include <sys/mman.h>
#    include <sys/stat.h>
#    include <unistd.h>
#else
#    include <fstream>
#endif

/// \brief Main library namespace.
///
/// The name of this is purposefully verbose to avoid name collisions. Users are
/// recommended to use a namespace alias instead.
namespace command_line_options {
/// ===========================================================================
///  Internals.
/// ===========================================================================
namespace detail { // clang-format off
/// Some compilers do not have __builtin_strlen().
#if defined __GNUC__ || defined __clang__
#    define CLOPTS_STRLEN(str)  __builtin_strlen(str)
#    define CLOPTS_STRCMP(a, b) __builtin_strcmp(a, b)
#else
constexpr inline std::size_t CLOPTS_STRLEN(const char* str) {
    std::size_t len = 0;
    while (*str++) ++len;
    return len;
}

constexpr inline int CLOPTS_STRCMP(const char* a, const char* b) {
    while (*a && *b && *a == *b) {
        ++a;
        ++b;
    }
    return *a - *b;
}

#    define CLOPTS_STRLEN(str)  ::command_line_options::detail::CLOPTS_STRLEN(str)
#    define CLOPTS_STRCMP(a, b) ::command_line_options::detail::CLOPTS_STRCMP(a, b)
#endif

/// \brief Helper to facilitate iteration over a parameter pack.
///
/// Use with caution because it can raise an ICE when compiling with recent versions of GCC.
///
/// This loops over \c pack, so long as \c cond is true, making each element of
/// the pack accessible under the name \c name. The body is wrapped in a loop so
/// we can use \c break to exit early.
#define CLOPTS_LOOP(name, pack, cond, ...) \
    ([&] <typename name> () -> bool { if (cond) { __VA_ARGS__ return true; } return false; }.template operator() <pack> () and ...)

/// Raise a compile-time error.
#ifndef CLOPTS_ERR
#    define CLOPTS_ERR(msg) [] <bool _x = false> { static_assert(_x, msg); } ()
#endif

/// `if constexpr`, but as an expression.
/// Use with caution because it can raise an ICE when compiling with recent versions of GCC.
#define CLOPTS_COND(cond, t, f) [&]<bool _x = cond>() { if constexpr (_x) return t; else return f; } ()

/// Check if two types are the same.
template <typename a, typename ...bs>
concept is = (std::is_same_v<std::remove_cvref_t<a>, std::remove_cvref_t<bs>> or ...);

/// Check if two types are exactly the same.
template <typename a, typename ...bs>
concept is_same = (std::is_same_v<a, bs> or ...);

/// Check if an operand type is a vector.
template <typename t> struct is_vector_s;
template <typename t> struct is_vector_s<std::vector<t>> { static constexpr bool value = true; };
template <typename t> struct is_vector_s { static constexpr bool value = false; };
template <typename t> concept is_vector_v = is_vector_s<t>::value;

/// Get the base type of an option.
template <typename t> struct base_type_s;
template <typename t> struct base_type_s<std::vector<t>> { using type = t; };
template <typename t> struct base_type_s { using type = t; };
template <typename t> using base_type_t = typename base_type_s<t>::type;

/// Check if an option is a positional option.
template <typename opt>
struct is_positional {
    static constexpr bool value = requires {{typename opt::is_positional_{}} -> std::same_as<std::true_type>; };
    using type = std::bool_constant<value>;
};

template <typename opt> using positional_t = typename is_positional<opt>::type;
template <typename opt> concept is_positional_v = is_positional<opt>::value;

/// Callback that takes an argument.
using callback_arg_type = void (*)(void*, std::string_view, std::string_view);

/// Callback that takes no arguments.
using callback_noarg_type = void (*)(void*, std::string_view);

/// Check whether a type is a callback.
template <typename T>
concept is_callback = is<T,
    callback_arg_type,
    callback_noarg_type,
    std::vector<detail::callback_arg_type>,
    std::vector<detail::callback_noarg_type>
>;

/// Check if an option type takes an argument.
template <typename type>
concept has_argument = not is<type, bool, callback_noarg_type>;

/// Whether we should include the argument type of an option in the
/// help text. This is true for all options that take arguments, except
/// the builtin help option.
template <typename opt>
concept should_print_argument_type = has_argument<typename opt::type> and not requires { opt::is_help_option; };

/// Wrap an arbitrary function in a lambda.
template <auto cb> struct make_lambda_s;

template <auto cb>
requires std::is_invocable_v<decltype(cb)>
struct make_lambda_s<cb> {
    using lambda = decltype([](void*, std::string_view) { cb(); });
    using type = callback_noarg_type;
};

template <auto cb>
requires std::is_invocable_v<decltype(cb), void*>
struct make_lambda_s<cb> {
    using lambda = decltype([](void* data, std::string_view) { cb(data); });
    using type = callback_noarg_type;
};

template <auto cb>
requires std::is_invocable_v<decltype(cb), std::string_view>
struct make_lambda_s<cb> {
    using lambda = decltype([](void*, std::string_view, std::string_view arg) { cb(arg); });
    using type = callback_arg_type;
};

template <auto cb>
requires std::is_invocable_v<decltype(cb), void*, std::string_view>
struct make_lambda_s<cb> {
    using lambda = decltype([](void* data, std::string_view, std::string_view arg) { cb(data, arg); });
    using type = callback_arg_type;
};

template <auto cb>
requires std::is_invocable_v<decltype(cb), std::string_view, std::string_view>
struct make_lambda_s<cb> {
    using lambda = decltype([](void*, std::string_view name, std::string_view arg) { cb(name, arg); });
    using type = callback_arg_type;
};

template <auto cb>
requires std::is_invocable_v<decltype(cb), void*, std::string_view, std::string_view>
struct make_lambda_s<cb> {
    using lambda = decltype([](void* data, std::string_view name, std::string_view arg) { cb(data, name, arg); });
    using type = callback_arg_type;
};

template <auto cb>
using make_lambda = make_lambda_s<cb>; // clang-format on

/// Execute code at end of scope.
template <typename lambda>
struct at_scope_exit {
    lambda l;
    ~at_scope_exit() { l(); }
};

/// Compile-time string.
template <size_t sz>
struct static_string {
    char data[sz]{};
    size_t len{};

    /// Construct an empty string.
    constexpr static_string() {}

    /// Construct from a string literal.
    constexpr static_string(const char (&_data)[sz]) {
        std::copy_n(_data, sz, data);
        len = sz - 1;
    }

    /// Check if two strings are equal.
    template <typename str>
    [[nodiscard]] constexpr bool operator==(const str& s) const {
        return len == s.len && CLOPTS_STRCMP(data, s.data) == 0;
    }

    /// Append to this string.
    template <size_t n>
    constexpr void operator+=(const static_string<n>& str) {
        static_assert(len + str.len < sz, "Cannot append string because it is too long");
        std::copy_n(str.data, str.len, data + len);
        len += str.len;
    }

    /// Append a string literal to this string.
    constexpr void append(const char* str) { append(str, CLOPTS_STRLEN(str)); }

    /// Append a string literal with a known length to this string.
    constexpr void append(const char* str, size_t length) {
        std::copy_n(str, length, data + len);
        len += length;
    }

    /// Get the string as a \c std::string_view.
    [[nodiscard]] constexpr auto sv() const -> std::string_view { return {data, len}; }
};

/// Default help handler.
inline void default_help_handler(std::string_view msg) {
    std::cerr << msg;
    std::exit(0);
}

} // namespace detail

/// ===========================================================================
///  Option types.
/// ===========================================================================
/// Forward-declare for friend decls.
template <typename... opts>
class clopts;

/// Base option type.
template <
    detail::static_string _name,
    detail::static_string _description = "",
    typename _type = std::string,
    bool required = false>
struct option {
    /// Make sure this is a valid option.
    static_assert(sizeof _description.data < 512, "Description may not be longer than 512 characters");
    static_assert(_name.len > 0, "Option name may not be empty");
    static_assert(sizeof _name.data < 256, "Option name may not be longer than 256 characters");
    static_assert(!std::is_void_v<_type>, "Option type may not be void. Use bool instead");
    static_assert( // clang-format off
        detail::is_same<_type, std::string,
            bool,
            double,
            int64_t,
            detail::callback_arg_type,
            detail::callback_noarg_type
        > or detail::is_vector_v<_type> or requires { _type::is_file_data; },
        "Option type must be std::string, bool, int64_t, double, file_data, callback, or a vector thereof"
    ); // clang-format on

    using type = _type;
    static constexpr inline decltype(_name) name = _name;
    static constexpr inline decltype(_description) description = _description;
    static constexpr inline bool is_flag = std::is_same_v<_type, bool>;
    static constexpr inline bool is_required = required;
    static constexpr inline bool option_tag = true;

    constexpr option() = delete;
};

/// A file.
template <typename contents_type_t = std::string>
struct file {
    using contents_type = contents_type_t;
    using element_type = typename contents_type::value_type;
    using element_pointer = std::add_pointer_t<element_type>;
    static constexpr bool is_file_data = true;

public:
    /// The file path.
    std::filesystem::path path;

    /// The contents of the file.
    contents_type contents;
};

/// For backwards compatibility.
using file_data = file<>;

/// A positional option.
template <
    detail::static_string _name,
    detail::static_string _description,
    typename _type = std::string,
    bool required = true>
struct positional : option<_name, _description, _type, required> {
    using is_positional_ = std::true_type;
};

/// Func option implementation.
template <
    detail::static_string _name,
    detail::static_string _description,
    typename lambda,
    bool required = false>
struct func_impl : option<_name, _description, typename lambda::type, required> {
    static constexpr inline typename lambda::lambda callback = {};
};

/// A function option.
template <
    detail::static_string _name,
    detail::static_string _description,
    auto cb,
    bool required = false>
struct func : func_impl<_name, _description, detail::make_lambda<cb>, required> {};

/// A flag option.
template <
    detail::static_string _name,
    detail::static_string _description = "",
    bool required = false>
struct flag : option<_name, _description, bool, required> {};

/// The help option.
template <auto callback = detail::default_help_handler>
struct help : func<"--help", "Print this help information", callback> {
    static constexpr inline bool is_help_option = true;
};

/// Multiple meta-option.
template <typename opt>
struct multiple : option<opt::name, opt::description, std::vector<typename opt::type>, opt::is_required> {
    using base_type = typename opt::type;
    using type = std::vector<typename opt::type>;
    static_assert(not detail::is<base_type, bool>, "Type of multiple<> cannot be bool");
    static_assert(not detail::is<base_type, detail::callback_arg_type>, "Type of multiple<> cannot be a callback");
    static_assert(not detail::is<base_type, detail::callback_noarg_type>, "Type of multiple<> cannot be a callback");

    constexpr multiple() = delete;
    static constexpr inline bool is_multiple = true;
    using is_positional_ = detail::positional_t<opt>;
};

/// ===========================================================================
///  Main implementation.
/// ===========================================================================
template <typename... opts>
class clopts {
    /// This should never be instantiated.
    constexpr clopts() = delete;
    constexpr ~clopts() = delete;
    clopts(const clopts& o) = delete;
    clopts(clopts&& o) = delete;
    clopts& operator=(const clopts& o) = delete;
    clopts& operator=(clopts&& o) = delete;

    /// Make sure no two options have the same name.
    static consteval bool check_duplicate_options() {
        bool has_duplicate = false;
        CLOPTS_LOOP(opt, opts, not has_duplicate, {
            CLOPTS_LOOP(opt2, opts, not has_duplicate, {
                has_duplicate = opt::name == opt2::name;
            });
        });
        return has_duplicate;
    }

    /// Make sure there is at most one multiple<positional<>> option.
    static consteval size_t validate_multiple() {
        return (... + (requires { opts::is_multiple; } and detail::is_positional_v<opts>) );
    }

    /// Make sure we don’t have invalid option combinations.
    static_assert(check_duplicate_options(), "Two different options may not have the same name");
    static_assert(validate_multiple() <= 1, "Cannot have more than one multiple<positional<>> option");

    /// Various types.
    using help_string_t = detail::static_string<1024 * sizeof...(opts)>;
    using string = std::string;
    using integer = int64_t;

    /// Variables for the parser and for storing parsed options.
    static inline bool has_error;
    static inline int argc;
    static inline int argi;
    static inline char** argv;
    static inline void* user_data;
    static inline std::tuple<typename opts::type...> optvals;
    static inline std::array<bool, sizeof...(opts)> opts_found{};
    static constexpr inline std::array<const char*, sizeof...(opts)> opt_names{opts::name.data...};
    static inline bool opts_parsed{};

    /// Get the index of an option.
    template <size_t index, detail::static_string option>
    static constexpr size_t optindex_impl() {
        if constexpr (index >= sizeof...(opts)) return index;
        else if constexpr (CLOPTS_STRCMP(opt_names[index], option.data) == 0) return index;
        else return optindex_impl<index + 1, option>();
    }

    /// Get the index of an option and raise an error if the option is not found.
    template <detail::static_string option>
    static constexpr size_t optindex() {
        constexpr size_t sz = optindex_impl<0, option>();
        static_assert(sz < sizeof...(opts), "Invalid option name. You've probably misspelt an option.");
        return sz;
    }

    /// Check if an option was found.
    template <detail::static_string option>
    static constexpr bool found() { return opts_found[optindex<option>()]; }

    /// Mark an option as found.
    template <detail::static_string option>
    static constexpr void set_found() { opts_found[optindex<option>()] = true; }

    /// Get a reference to an option value.
    template <detail::static_string s>
    [[nodiscard]] static constexpr auto ref() -> decltype(std::get<optindex<s>()>(optvals))& {
        using value_type = decltype(std::get<optindex<s>()>(optvals));

        /// Bool options don’t have a value.
        if constexpr (std::is_same_v<value_type, bool>) CLOPTS_ERR("Cannot call ref() on an option<bool>");

        /// Function options don’t have a value.
        else if constexpr (detail::is_callback<value_type>) CLOPTS_ERR("Cannot call ref<>() on an option with function type.");

        /// Get the option value.
        else return std::get<optindex<s>()>(optvals);
    }

    /// Get the type of an option value.
    template <detail::static_string s>
    using optval_t = std::remove_cvref_t<decltype(std::get<optindex<s>()>(optvals))>;

    /// This implements get<>() and get_or<>().
    template <detail::static_string s>
    static constexpr auto get_impl() -> std::conditional_t<std::is_same_v<optval_t<s>, bool>, bool, optval_t<s>*> {
        using value_type = optval_t<s>;

        /// Bool options don’t have a value. Instead, we just return whether the option was found.
        if constexpr (std::is_same_v<value_type, bool>) return opts_found[optindex<s>()];

        /// We always return a pointer to vector options because the user can just check if it’s empty.
        else if constexpr (detail::is_vector_v<value_type>) return std::addressof(std::get<optindex<s>()>(optvals));

        /// Function options don’t have a value.
        else if constexpr (detail::is_callback<value_type>) CLOPTS_ERR("Cannot call get<>() on an option with function type.");

        /// Otherwise, return nullptr if the option wasn’t found, and a pointer to the value otherwise.
        else return not opts_found[optindex<s>()] ? nullptr : std::addressof(std::get<optindex<s>()>(optvals));
    }

public:
    /// Get the value of an option.
    ///
    /// This is not [[nodiscard]] because that raises an ICE when compiling
    /// with some older versions of GCC.
    ///
    /// \return true/false if the option is a flag
    /// \return nullptr if the option was not found
    /// \return a pointer to the value if the option was found
    template <detail::static_string s>
    static constexpr auto get() {
        /// Check if the option exists before calling get_impl<>() so we trigger the static_assert
        /// below before hitting a complex template instantiation error.
        constexpr auto sz = optindex_impl<0, s>();
        if constexpr (sz < sizeof...(opts)) return get_impl<s>();
        else static_assert(sz < sizeof...(opts), "Invalid option name. You've probably misspelt an option.");
    }

    /// Get the value of an option or a default value if the option was not found.
    ///
    /// \param default_ The default value to return if the option was not found.
    /// \return default_ if the option was not found.
    /// \return a copy of the option value if the option was found.
    template <detail::static_string s>
    static constexpr auto get_or(auto default_) {
        constexpr auto sz = optindex_impl<0, s>();
        if constexpr (sz < sizeof...(opts)) {
            if (!opts_found[optindex<s>()]) return static_cast<std::remove_cvref_t<decltype(*get_impl<s>())>>(default_);
            return *get_impl<s>();
        } else {
            static_assert(sz < sizeof...(opts), "Invalid option name. You've probably misspelt an option.");
        }
    }

private:
    /// Create the help message.
    static constexpr auto make_help_message() -> help_string_t { // clang-format off
        help_string_t msg{};

        /// Append the positional options.
        CLOPTS_LOOP(opt, opts, true, {
            if constexpr (detail::is_positional_v<opt>) {
                msg.append("<");
                msg.append(opt::name.data, opt::name.len);
                msg.append("> ");
            }
        });

        /// End of first line.
        msg.append("[options]\n");

        /// Start of options list.
        msg.append("Options:\n");

        /// Determine the length of the longest name + typename so that
        /// we know how much padding to insert before actually printing
        /// the description. Also factor in the <> signs around and the
        /// space after the option name, as well as the type name.
        size_t max_len{};
        auto determine_length = [&]<typename opt> {
            /// Positional options go on the first line, so ignore them here.
            if constexpr (not detail::is_positional_v<opt>) {
                max_len = std::max(max_len, CLOPTS_COND (
                    detail::should_print_argument_type<opt>,
                    opt::name.len + (type_name<typename opt::type>().len + sizeof("<> ") - 1),
                    opt::name.len
                ));
            }
        };
        (determine_length.template operator()<opts>(), ...);

        /// Append the options
        auto append = [&] <typename opt> {
            /// Positional options have already been handled.
            if constexpr (not detail::is_positional_v<opt>) {
                /// Append the name.
                msg.append("    ");
                msg.append(opt::name.data, opt::name.len);

                /// Compute the padding for this option and append the type name.
                size_t len = opt::name.len;
                if constexpr (detail::should_print_argument_type<opt>) {
                    const auto tname = type_name<typename opt::type>();
                    len += (3 + tname.len);
                    msg.append(" <");
                    msg.append(tname.data, tname.len);
                    msg.append(">");
                }

                /// Append the padding.
                for (size_t i = 0; i < max_len - len; i++) msg.append(" ");

                /// Append the description.
                msg.append("  ");
                msg.append(opt::description.data, opt::description.len);
                msg.append("\n");
            }
        };
        (append.template operator()<opts>(), ...);

        /// Return the combined help message.
        return msg;
    }; // clang-format on

    /// Help message is created at compile time.
    static constexpr inline help_string_t help_message_raw = make_help_message();

public:
    /// Get the help message.
    static auto help() -> std::string {
        std::string msg = "Usage: ";
        msg += argv[0];
        msg += " ";
        msg.append(help_message_raw.data, help_message_raw.len);
        return msg;
    }

private:
    /// Handle an option value.
    template <typename opt, bool is_multiple>
    static auto dispatch_option_with_arg(std::string_view opt_str, std::string_view opt_val) {
        using opt_type = typename opt::type;

        /// Mark the option as found.
        set_found<opt::name>();

        /// If this is a function option, simply call the callback and we're done.
        if constexpr (detail::is_callback<opt_type>) {
            if constexpr (detail::is<opt_type, detail::callback_noarg_type>) opt::callback(user_data, opt_str);
            else opt::callback(user_data, opt_str, opt_val);
        }

        /// Otherwise, parse the argument.
        else {
            /// Handle the argument.
            if constexpr (is_multiple) ref<opt::name>().push_back(make_arg<opt_type>(opt_val));
            else ref<opt::name>() = make_arg<opt_type>(opt_val);
        }
    }

    /// This callback is called whenever an error occurs during parsing.
    ///
    /// \param errmsg An error message that describes what went wrong.
    /// \return `true` if the parsing process should continue, and `false` otherwise.
    static inline std::function<bool(std::string&&)> error_handler = [](std::string&& errmsg) -> bool {
        std::cerr << argv[0] << ": " << errmsg << "\n";

        /// Invoke the help option.
        bool invoked = false;
        auto invoke = [&]<typename opt> {
            if constexpr (requires { opt::is_help_option; }) {
                invoked = true;
                dispatch_option_with_arg<opt, false>("--help", help_message_raw.sv());
            }
        };

        /// If there is a help option, invoke it.
        (invoke.template operator()<opts>(), ...);

        /// If no help option was found, print the help message.
        if (not invoked) std::cerr << help();
        std::exit(1);
    };

    /// Invoke the error handler and set the error flag.
    static void handle_error(auto first, auto&&... msg_parts) {
        /// Append the message parts.
        std::string msg = std::string{std::move(first)};
        ((msg += std::forward<decltype(msg_parts)>(msg_parts)), ...);

        /// Dispatch the error.
        has_error = not error_handler(std::move(msg));
    }

    /// Get the name of an option type.
    template <typename t>
    static consteval auto type_name() -> detail::static_string<25> {
        detail::static_string<25> buffer;
        if constexpr (detail::is<t, string>) buffer.append("string");
        else if constexpr (detail::is<t, bool>) buffer.append("bool");
        else if constexpr (detail::is<t, integer, double>) buffer.append("number");
        else if constexpr (requires { t::is_file_data; }) buffer.append("file");
        else if constexpr (detail::is_callback<t>) buffer.append("arg");
        else if constexpr (detail::is_vector_v<t>) {
            buffer.append(type_name<typename t::value_type>().data, type_name<typename t::value_type>().len);
            buffer.append("s");
        } else {
            CLOPTS_ERR("Option type must be std::string, bool, integer, double, or void(*)(), or a vector thereof");
        }
        return buffer;
    }

    template <typename file_data_type>
    static file_data_type map_file(std::string_view path) {
        static const auto err = [](std::string_view p) -> file_data_type {
            std::string msg = "Could not read file \"";
            msg += p;
            msg += "\": ";
            msg += ::strerror(errno);
            handle_error(std::move(msg));
            return {};
        };

#if CLOPTS_USE_MMAP
        int fd = ::open(path.data(), O_RDONLY);
        if (fd < 0) return err(path);

        struct stat s {};
        if (::fstat(fd, &s)) return err(path);
        auto sz = size_t(s.st_size);
        if (sz == 0) return {};

        auto* mem = (char*) ::mmap(nullptr, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
        if (mem == MAP_FAILED) return err(path);
        ::close(fd);

        /// Construct the file contents.
        typename file_data_type::contents_type ret;
        auto pointer = reinterpret_cast<typename file_data_type::element_pointer>(mem);
        if constexpr (requires { ret.assign(pointer, sz); }) ret.assign(pointer, sz);
        else if constexpr (requires { ret.assign(pointer, pointer + sz); }) ret.assign(pointer, pointer + sz);
        else CLOPTS_ERR("file_data_type::contents_type must have an assign method that takes a pointer and a size_t (or a begin and end iterator) as arguments.");
        ::munmap(mem, sz);

#else
        using contents_type = typename file_data_type::contents_type;

        /// Read the file manually.
        auto f = std::fopen(path.data(), "rb");
        if (not f) return err(path);

        /// Get the file size.
        std::fseek(f, 0, SEEK_END);
        auto sz = std::size_t(std::ftell(f));
        std::fseek(f, 0, SEEK_SET);

        /// Read the file.
        contents_type ret;
        ret.resize(sz);
        std::size_t n_read = 0;
        while (n_read < sz) {
            auto n = std::fread(ret.data() + n_read, 1, sz - n_read, f);
            if (n < 0) return err(path);
            if (n == 0) break;
            n_read += n;
        }
#endif

        /// Construct the file data.
        file_data_type dat;
        dat.path = path;
        dat.contents = std::move(ret);
        return dat;
    }

    /// Helper to parse an integer or double.
    template <typename number_type, detail::static_string name>
    static auto parse_number(std::string_view s, auto parse_func) -> number_type {
        char* pos{};
        errno = 0;
        auto i = parse_func(s.data(), &pos, 10);
        if (errno == ERANGE or pos != s.end()) handle_error(s, " does not appear to be a valid ", name.sv());
        return number_type(i);
    }

    /// Parse an option value.
    template <typename type>
    static detail::base_type_t<type> make_arg(std::string_view opt_val) {
        using base_type = detail::base_type_t<type>;

        /// Make sure this option takes an argument.
        if constexpr (not detail::has_argument<base_type>) CLOPTS_ERR("This option type does not take an argument");

        /// Strings do not require parsing.
        else if constexpr (std::is_same_v<base_type, std::string>) return std::string{opt_val};

        /// If it’s a file, read its contents.
        else if constexpr (requires { base_type::is_file_data; }) return map_file<base_type>(opt_val);

        /// Parse an integer or double.
        else if constexpr (std::is_same_v<base_type, integer>) return parse_number<integer, "integer">(opt_val, std::strtoull);
        else if constexpr (std::is_same_v<base_type, double>) return parse_number<double, "floating-point number">(opt_val, std::strtod);

        /// Should never get here.
        else CLOPTS_ERR("Unreachable");
    }

    /// Handle an option that may take an argument.
    ///
    /// Both --option value and --option=value are valid ways of supplying a
    /// value. We test for both of them.
    template <typename opt, bool is_multiple>
    static bool handle_opt_with_arg(std::string_view opt_str) {
        using opt_type = typename opt::type;

        /// --option=value
        if (opt_str.size() > opt::name.len) {
            /// If the string starts with the option name not followed by '=', then
            /// this isn’t the right option.
            if (opt_str[opt::name.len] != '=') return false;

            /// Otherwise, parse the value.
            const auto opt_start_offs = opt::name.len + 1;
            const auto opt_name = opt_str.substr(0, opt_start_offs);
            const auto opt_val = opt_str.substr(opt_start_offs);
            dispatch_option_with_arg<opt, is_multiple>(opt_name, opt_val);
            return true;
        }

        /// Handle the option. If we get here, we know that the option name that we’ve
        /// encountered matches the option name exactly.
        else {
            /// If this is a func option that doesn’t take arguments, just call the callback and we’re done.
            if constexpr (detail::is<opt_type, detail::callback_noarg_type>) {
                opt::callback(user_data, opt_str);
                return true;
            }

            /// Otherwise, try to consume the next argument as the option value.
            else {
                /// No more command line arguments left.
                if (++argi == argc) {
                    handle_error("Missing argument for option \"", opt_str, "\"");
                    return false;
                }

                /// Parse the argument.
                dispatch_option_with_arg<opt, is_multiple>(opt_str, argv[argi]);
                return true;
            }
        }
    }

    /// Handle an option. The parser calls this on each non-positional option.
    template <typename opt>
    static bool handle_regular_impl(std::string_view opt_str) {
        /// If the supplied string doesn’t start with the option name, move on to the next option
        if (not opt_str.starts_with(opt::name.sv())) return false;

        /// Check if this option accepts multiple values.
        using base_type = detail::base_type_t<typename opt::type>;
        static constexpr bool is_multiple = requires { opt::is_multiple; };
        if constexpr (not is_multiple and not detail::is_callback<base_type>) {
            /// Duplicate options are not allowed by default.
            if (found<opt::name>()) {
                std::string errmsg;
                errmsg += "Duplicate option: \"";
                errmsg += opt_str;
                errmsg += "\"";
                handle_error(std::move(errmsg));
                return false;
            }
        }

        /// Flags and callbacks don't have arguments.
        if constexpr (not detail::has_argument<base_type>) {
            /// Check if the name of this flag matches the entire option string that
            /// we encountered. If we’re just a prefix, then we don’t handle this.
            if (opt_str != opt::name.sv()) return false;

            /// Mark the option as found. That’s all we need to do for flags.
            set_found<opt::name>();

            /// If it’s a callable, call it.
            if constexpr (detail::is_callback<base_type>) {
                /// The builtin help option is handled here. We pass the help message as an argument.
                if constexpr (requires { opt::is_help_option; }) dispatch_option_with_arg<opt, false>(opt_str, help_message_raw.sv());

                /// If it’s not the help option, just invoke it.
                else opt::callback(user_data, opt_str);
            }

            /// Option has been handled.
            return true;
        }

        /// Handle an option that may take an argument.
        else { return handle_opt_with_arg<opt, is_multiple>(opt_str); }
    }

#undef INVOKE

    template <typename opt>
    static bool handle_positional_impl(std::string_view opt_str) {
        /// If we've already encountered this positional option, then return.
        static constexpr bool is_multiple = requires { opt::is_multiple; };
        if constexpr (not is_multiple) {
            if (found<opt::name>()) return false;
        }

        /// Otherwise, attempt to parse this as the option value.
        set_found<opt::name>();
        if constexpr (is_multiple) ref<opt::name>().push_back(make_arg<typename opt::type>(opt_str));
        else ref<opt::name>() = make_arg<typename opt::type>(opt_str.data());
        return true;
    }

    /// Invoke handle_regular_impl on every option until one returns true.
    static bool handle_regular(std::string_view opt_str) {
        static const auto handle = []<typename opt>(std::string_view str) {
            if constexpr (detail::is_positional_v<opt>) return false;
            else return handle_regular_impl<opt>(str);
        };

        return (handle.template operator()<opts>(opt_str) or ...);
    };

    /// Invoke handle_positional_impl on every option until one returns true.
    static bool handle_positional(std::string_view opt_str) {
        static const auto handle = []<typename opt>(std::string_view str) {
            if constexpr (detail::is_positional_v<opt>) return handle_positional_impl<opt>(str);
            else return false;
        };

        return (handle.template operator()<opts>(opt_str) or ...);
    };

public:
    /// Parse command line options.
    ///
    /// \param _argc The argument count.
    /// \param _argv The arguments.
    /// \param _user_data User data passed to any func<> options that accept a `void*`.
    static void parse(int _argc, char** _argv, void* _user_data = nullptr) {
        /// Initialise state.
        has_error = false;
        argc = _argc;
        argv = _argv;
        user_data = _user_data;

        /// This is to silence any dangling pointer warnings about retaining `user_data`.
        detail::at_scope_exit<decltype([] { user_data = nullptr; })> guard;

        /// Check that we haven’t already parsed options.
        if (opts_parsed) {
            handle_error("Cannot parse options twice");
            return;
        } else {
            opts_parsed = true;
        }

        /// Main parser loop.
        for (argi = 1; argi < argc; argi++) {
            std::string_view opt_str{argv[argi]};

            /// Attempt to handle the option.
            if (not handle_regular(opt_str) and not handle_positional(opt_str)) {
                std::string errmsg;
                errmsg += "Unrecognized option: \"";
                errmsg += opt_str;
                errmsg += "\"";
                handle_error(std::move(errmsg));
            }

            /// Stop parsing if there was an error.
            if (has_error) return;
        }

        /// Make sure all required options were found.
        CLOPTS_LOOP(opt, opts, true, {
            if (not found<opt::name>() and opt::is_required) {
                std::string errmsg;
                errmsg += "Option \"";
                errmsg += opt::name.sv();
                errmsg += "\" is required";
                handle_error(std::move(errmsg));
            }
        });
    }
};

} // namespace command_line_options

#undef CLOPTS_STRLEN
#undef CLOPTS_STRCMP
#undef CLOPTS_ERR
#undef CLOPTS_COND
#undef CLOPTS_LOOP
#endif // CLOPTS_H
