#ifndef CLOPTS_H
#define CLOPTS_H

#include <algorithm>
#include <array>
#include <bitset>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

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
// ===========================================================================
//  Internals.
// ===========================================================================
namespace detail {
using namespace std::literals;

// ===========================================================================
//  Metaprogramming Helpers.
// ===========================================================================
/// List of types.
template <typename ...pack> struct list {
    /// Apply a function to each element of the list.
    static constexpr void each(auto&& lambda) {
        (lambda.template operator()<pack>(), ...);
    }
};

/// Concatenate two type lists.
template <typename, typename> struct concat_impl;
template <typename ...Ts, typename ...Us>
struct concat_impl<list<Ts...>, list<Us...>> {
    using type = list<Ts..., Us...>;
};

template <typename T, typename U>
using concat = typename concat_impl<T, U>::type;

// TODO: Use pack indexing once the syntax is fixed and compilers
// have actually started defining __cpp_pack_indexing.
template <std::size_t i, typename... pack>
using nth_type = std::tuple_element_t<i, std::tuple<pack...>>;

/// Filter a pack of types.
template <template <typename> typename, typename...>
struct filter_impl;

template < // clang-format off
    template <typename> typename cond,
    typename ...processed,
    typename next,
    typename ...rest
> struct filter_impl<cond, list<processed...>, next, rest...> {
    using type = std::conditional_t<cond<next>::value,
        filter_impl<cond, list<processed..., next>, rest...>,
        filter_impl<cond, list<processed...>, rest...>
    >::type;
}; // clang-format on

template <template <typename> typename cond, typename... processed>
struct filter_impl<cond, list<processed...>> {
    using type = list<processed...>;
};

template <template <typename> typename cond, typename... types>
using filter = typename filter_impl<cond, list<>, types...>::type;

/// See that one talk (by Daisy Hollman, I think) about how this works.
template <template <typename> typename get_key, typename... types>
struct sort_impl {
private:
    static constexpr auto sorter = []<std::size_t ...i>(std::index_sequence<i...>) {
        static constexpr auto sorted = [] {
            std::array indices{i...};
            std::array lookup_table{get_key<types>::value...};
            std::sort(indices.begin(), indices.end(), [&](std::size_t a, std::size_t b) {
                return lookup_table[a] < lookup_table[b];
            });
            return indices;
        }();
        return list<nth_type<sorted[i], types...>...>{};
    };

public:
    using type = decltype(sorter(std::index_sequence_for<types...>()));
};

template <template <typename> typename get_key, typename... types>
struct sort_impl<get_key, list<types...>> {
    using type = typename sort_impl<get_key, types...>::type;
};

// Special case because an array of size 0 is not going to work...
template <template <typename> typename get_key>
struct sort_impl<get_key, list<>> { using type = list<>; };

/// Sort a type list. The trick here is to sort the indices.
template <template <typename> typename get_key, typename type_list>
using sort = typename sort_impl<get_key, type_list>::type;

/// Iterate over a pack while a condition is true.
template <typename... pack>
constexpr void Foreach(auto&& lambda) {
    list<pack...>::each(std::forward<decltype(lambda)>(lambda));
}

/// Iterate over a pack while a condition is true.
template <typename... pack>
constexpr void While(bool& cond, auto&& lambda) {
    auto impl = [&]<typename t>() -> bool {
        if (not cond) return false;
        lambda.template operator()<t>();
        return true;
    };

    (impl.template operator()<pack>() and ...);
}

// ===========================================================================
//  Type Traits and Metaprogramming Types.
// ===========================================================================
/// Empty type.
struct empty {};

/// Check if two types are the same.
template <typename a, typename ...bs>
concept is = (std::is_same_v<std::remove_cvref_t<a>, std::remove_cvref_t<bs>> or ...);

/// Check if two types are exactly the same.
template <typename a, typename ...bs>
concept is_same = (std::is_same_v<a, bs> or ...);

/// Check if an operand type is a vector.
template <typename t> struct test_vector;
template <typename t> struct test_vector<std::vector<t>> {
    static constexpr bool value = true;
    using type = t;
};

template <typename t> struct test_vector {
    static constexpr bool value = false;
    using type = t;
};

template <typename t> concept is_vector_v = test_vector<t>::value;
template <typename t> using remove_vector_t = typename test_vector<t>::type;

/// Check if an option is a positional option.
template <typename opt>
struct is_positional {
    static constexpr bool value = requires {{typename opt::is_positional_{}} -> std::same_as<std::true_type>; };
    using type = std::bool_constant<value>;
};

template <typename opt>
struct is_not_positional {
    static constexpr bool value = not is_positional<opt>::value;
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
    std::vector<callback_arg_type>,
    std::vector<callback_noarg_type>
>;

/// Check if an option type takes an argument.
template <typename type>
concept has_argument = not is<type, bool, callback_noarg_type>;

/// Whether we should include the argument type of an option in the
/// help text. This is true for all options that take arguments, except
/// the builtin help option.
template <typename opt>
concept should_print_argument_type = has_argument<typename opt::type> and not requires { opt::is_help_option; };

/// Helper for static asserts.
template <typename t>
concept always_false = false;

/// Not a concept because we can’t pass packs as the first parameter of a concept.
template <typename first, typename ...rest>
static constexpr bool assert_same_type = (std::is_same_v<first, rest> and ...);

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

template <typename first, typename...>
struct first_type {
    using type = first;
};

/// Get the first element of a pack.
template <typename... rest>
using first_type_t = typename first_type<rest...>::type;

/// Execute code at end of scope.
template <typename lambda>
struct at_scope_exit {
    lambda l;
    ~at_scope_exit() { l(); }
};

/// Tag used for options that modify the options (parser) but
/// do not constitute actual options in an of themselves.
struct special_tag;

// ===========================================================================
//  Compile-Time String.
// ===========================================================================
// Some compilers do not have __builtin_strlen().
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

/// Raise a compile-time error.
#ifndef CLOPTS_ERR
#    define CLOPTS_ERR(msg) [] <bool _x = false> { static_assert(_x, msg); } ()
#endif

/// Constexpr to_string for integers. Returns the number of bytes written.
constexpr std::size_t constexpr_to_string(char* out, std::int64_t i) {
    // Special handling for 0.
    if (i == 0) {
        *out = '0';
        return 1;
    }

    const auto start = out;
    if (i < 0) {
        *out++ = '-';
        i = -i;
    }

    while (i) {
        *out++ = char('0' + char(i % 10));
        i /= 10;
    }

    std::reverse(start, out);
    return std::size_t(out - start);
}

/// Compile-time string.
template <size_t sz>
struct static_string {
    char arr[sz]{};
    size_t len{};

    /// Construct an empty string.
    constexpr static_string() {}

    /// Construct from a string literal.
    constexpr static_string(const char (&_data)[sz]) {
        std::copy_n(_data, sz, arr);
        len = sz - 1;
    }

    /// Check if two strings are equal.
    template <typename str>
    requires requires { std::declval<str>().len; }
    [[nodiscard]] constexpr bool operator==(const str& s) const {
        return len == s.len && CLOPTS_STRCMP(arr, s.arr) == 0;
    }

    /// Check if this is equal to a string.
    [[nodiscard]] constexpr bool operator==(std::string_view s) const {
        return sv() == s;
    }

    /// Append to this string.
    template <size_t n>
    constexpr void operator+=(const static_string<n>& str) {
        static_assert(len + str.len < sz, "Cannot append string because it is too long");
        std::copy_n(str.arr, str.len, arr + len);
        len += str.len;
    }

    /// Append a string literal to this string.
    constexpr void append(const char* str) { append(str, CLOPTS_STRLEN(str)); }

    /// Append a string literal with a known length to this string.
    constexpr void append(const char* str, size_t length) {
        std::copy_n(str, length, arr + len);
        len += length;
    }

    /// Get the string as a \c std::string_view.
    [[nodiscard]] constexpr auto sv() const -> std::string_view { return {arr, len}; }

    /// API for static_assert.
    [[nodiscard]] constexpr auto data() const -> const char* { return arr; }
    [[nodiscard]] constexpr auto size() const -> std::size_t { return len; }

    static constexpr bool is_static_string = true;
};

/// Deduction guide to shut up nonsense CTAD warnings.
template <size_t sz>
static_string(const char (&)[sz]) -> static_string<sz>;

template <std::size_t size>
struct string_or_int {
    static_string<size> s{};
    std::int64_t integer{};
    bool is_integer{};

    constexpr string_or_int(const char (&data)[size]) {
        std::copy_n(data, size, s.arr);
        s.len = size - 1;
        is_integer = false;
    }

    constexpr string_or_int(std::int64_t integer)
        : integer{integer}
        , is_integer{true} {}
};

string_or_int(std::int64_t) -> string_or_int<1>;

// ===========================================================================
//  Types.
// ===========================================================================
/// Struct for storing allowed option values.
template <typename _type, auto... data>
struct values_impl {
    using type = _type;
    constexpr values_impl() = delete;

    static constexpr bool is_valid_option_value(const type& val) {
        auto test = [val]<auto value>() -> bool {
            if constexpr (value.is_integer) return value.integer == val;
            else return value.s == val;
        };

        return (test.template operator()<data>() or ...);
    }

    static constexpr auto print_values(char* out) -> std::size_t {
        // TODO: Wrap and indent every 10 or so values?
        bool first = true;
        auto append = [&]<auto value>() -> std::size_t {
            if (first) first = false;
            else {
                std::copy_n(", ", 2, out);
                out += 2;
            }
            if constexpr (value.is_integer) {
                char s[32]{};
                auto len = constexpr_to_string(s, value.integer);
                std::copy_n(s, len, out);
                out += len;
                return len;
            } else {
                std::copy_n(value.s.arr, value.s.len, out);
                out += value.s.len;
                return value.s.len;
            }
        };
        return (append.template operator()<data>() + ...) + (sizeof...(data) - 1) * 2;
    }
};

template <string_or_int... data>
concept values_must_be_all_strings_or_all_ints = (data.is_integer and ...) or (not data.is_integer and ...);

/// Values type.
template <string_or_int... data>
requires values_must_be_all_strings_or_all_ints<data...>
struct values : values_impl<std::conditional_t<(data.is_integer and ...), std::int64_t, std::string>, data...> {};

template <typename _type, static_string...>
struct ref {
    using type = _type;
};

/// Check that an option type is valid.
template <typename type>
concept is_valid_option_type = is_same<type, std::string, // clang-format off
    bool,
    double,
    int64_t,
    special_tag,
    callback_arg_type,
    callback_noarg_type
> or is_vector_v<type> or requires { type::is_values; } or requires { type::is_file_data; };
// clang-format on

template <typename _type>
struct option_type {
    using type = _type;
    static constexpr bool is_values = false;
    static constexpr bool is_ref = false;
};

/// Look through values<> to figure out the option type.
template <auto... vs>
struct option_type<values<vs...>> {
    using type = values<vs...>::type;
    static constexpr bool is_values = true;
    static constexpr bool is_ref = false;
};

/// And ref<> too.
template <typename _type, auto... vs>
struct option_type<ref<_type, vs...>> {
    using type = ref<_type, vs...>::type;
    static constexpr bool is_values = false;
    static constexpr bool is_ref = true;
};

template <typename _type>
using option_type_t = typename option_type<_type>::type;

template <typename _type>
concept is_values_type_t = option_type<_type>::is_values;

// ===========================================================================
//  Option Implementation.
// ===========================================================================
template <
    static_string _name,
    static_string _description,
    typename ty_param,
    bool required,
    bool overridable>
struct opt_impl {
    // There are four possible cases we need to handle here:
    //   - Simple type: std::string, int64_t, ...
    //   - Vector of simple type: std::vector<std::string>, std::vector<int64_t>, ...
    //   - Values or ref type: values<...>, ref<...>
    //   - Vector of values or ref type: std::vector<values<...>>

    /// The actual type that was passed in.
    using declared_type = ty_param;

    /// The type stripped of top-level std::vector<>.
    using declared_type_base = remove_vector_t<declared_type>;

    /// The underlying simple type used to store one element.
    using single_element_type = option_type_t<declared_type_base>;

    /// Single element type with vector readded.
    using canonical_type = std::conditional_t<is_vector_v<declared_type>, std::vector<single_element_type>, single_element_type>;

    /// Make sure this is a valid option.
    static_assert(sizeof _description.arr < 512, "Description may not be longer than 512 characters");
    static_assert(_name.len > 0, "Option name may not be empty");
    static_assert(sizeof _name.arr < 256, "Option name may not be longer than 256 characters");
    static_assert(not std::is_void_v<canonical_type>, "Option type may not be void. Use bool instead");
    static_assert(
        is_valid_option_type<canonical_type>,
        "Option type must be std::string, bool, int64_t, double, file_data, values<>, or callback"
    );

    static constexpr decltype(_name) name = _name;
    static constexpr decltype(_description) description = _description;
    static constexpr bool is_flag = std::is_same_v<canonical_type, bool>;
    static constexpr bool is_values = is_values_type_t<declared_type_base>;
    static constexpr bool is_ref = option_type<declared_type_base>::is_ref;
    static constexpr bool is_required = required;
    static constexpr bool is_overridable = overridable;
    static constexpr bool option_tag = true;
    static_assert(not is_flag or not is_ref, "Flags cannot reference other options"); // TODO: Allow this?

    static constexpr bool is_valid_option_value(
        const single_element_type& val
    ) {
        if constexpr (is_values) return declared_type_base::is_valid_option_value(val);
        else return true;
    }

    static constexpr auto print_values(char* out) -> std::size_t {
        if constexpr (is_values) return declared_type_base::print_values(out);
        else return 0;
    }

    constexpr opt_impl() = delete;
};

// ===========================================================================
//  Parser Helpers.
// ===========================================================================
/// Default help handler.
[[noreturn]] inline void default_help_handler(std::string_view program_name, std::string_view msg) {
    std::cerr << "Usage: " << program_name << " " << msg;
    std::exit(1);
}

template <typename file_data_type>
static file_data_type map_file(
    std::string_view path,
    auto error_handler = [](std::string&& msg) { std::cerr << msg << "\n"; std::exit(1); }
) {
    const auto err = [&](std::string_view p) -> file_data_type {
        std::string msg = "Could not read file \"";
        msg += p;
        msg += "\": ";
        msg += ::strerror(errno);
        error_handler(std::move(msg));
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

    // Construct the file contents.
    typename file_data_type::contents_type ret;
    auto pointer = reinterpret_cast<typename file_data_type::element_pointer>(mem);
    if constexpr (requires { ret.assign(pointer, sz); }) ret.assign(pointer, sz);
    else if constexpr (requires { ret.assign(pointer, pointer + sz); }) ret.assign(pointer, pointer + sz);
    else CLOPTS_ERR("file_data_type::contents_type must have a callable assign member that takes a pointer and a size_t (or a begin and end iterator) as arguments.");
    ::munmap(mem, sz);

#else
    using contents_type = typename file_data_type::contents_type;

    // Read the file manually.
    std::unique_ptr<FILE, decltype(&std::fclose)> f{std::fopen(path.data(), "rb"), std::fclose};
    if (not f) return err(path);

    // Get the file size.
    std::fseek(f.get(), 0, SEEK_END);
    auto sz = std::size_t(std::ftell(f.get()));
    std::fseek(f.get(), 0, SEEK_SET);

    // Read the file.
    contents_type ret;
    ret.resize(sz);
    std::size_t n_read = 0;
    while (n_read < sz) {
        auto n = std::fread(ret.data() + n_read, 1, sz - n_read, f.get());
        if (n < 0) return err(path);
        if (n == 0) break;
        n_read += n;
    }
#endif

    // Construct the file data.
    file_data_type dat;
    dat.path = typename file_data_type::path_type{path.begin(), path.end()};
    dat.contents = std::move(ret);
    return dat;
}

/// Get the name of an option type.
template <typename t>
static consteval auto type_name() -> static_string<25> {
    static_string<25> buffer;
    if constexpr (detail::is<t, std::string>) buffer.append("string");
    else if constexpr (detail::is<t, bool>) buffer.append("bool");
    else if constexpr (detail::is<t, std::int64_t, double>) buffer.append("number");
    else if constexpr (requires { t::is_file_data; }) buffer.append("file");
    else if constexpr (detail::is_callback<t>) buffer.append("arg");
    else if constexpr (detail::is_vector_v<t>) {
        buffer.append(type_name<typename t::value_type>().arr, type_name<typename t::value_type>().len);
        buffer.append("s");
    } else {
        CLOPTS_ERR("Option type must be std::string, bool, integer, double, or void(*)(), or a vector thereof");
    }
    return buffer;
}

// ===========================================================================
//  Sort/filter helpers.
// ===========================================================================
template <typename opt>
struct get_option_name {
    static constexpr std::string_view value = opt::name.sv();
};

template <typename opt>
struct is_values_option {
    static constexpr bool value = opt::is_values;
};

/// Check if an option is a regular option.
template <typename opt>
struct regular_option {
    static constexpr bool value = not is<typename opt::canonical_type, special_tag>;
};

/// Check if an option is a special option.
template <typename opt>
struct special_option {
    static constexpr bool value = not regular_option<opt>::value;
};

// ===========================================================================
//  Main Implementation.
// ===========================================================================
template <typename... opts>
class clopts_impl;

template <typename... opts, typename... special>
class clopts_impl<list<opts...>, list<special...>> {
    // This should never be instantiated by the user.
    explicit clopts_impl() = default;
    ~clopts_impl() = default;
    clopts_impl(const clopts_impl& o) = delete;
    clopts_impl(clopts_impl&& o) = delete;
    clopts_impl& operator=(const clopts_impl& o) = delete;
    clopts_impl& operator=(clopts_impl&& o) = delete;

    // =======================================================================
    //  Option Access by Name.
    // =======================================================================
    /// Option names.
    static constexpr std::array<const char*, sizeof...(opts)> opt_names{opts::name.arr...};

    /// Get the index of an option.
    template <size_t index, static_string option>
    static constexpr size_t optindex_impl() {
        if constexpr (index >= sizeof...(opts)) return index;
        else if constexpr (CLOPTS_STRCMP(opt_names[index], option.arr) == 0) return index;
        else return optindex_impl<index + 1, option>();
    }

#if __cpp_static_assert >= 202306L
    template <static_string option>
    static consteval auto format_invalid_option_name() -> static_string<option.size() + 45> {
        static_string<option.size() + 45> ret;
        ret.append("There is no option with the name '");
        ret.append(option.data(), option.size());
        ret.append("'");
        return ret;
    }
#endif

    template <bool ok, static_string option>
    static consteval void assert_valid_option_name() {
#if __cpp_static_assert >= 202306L
        static_assert(ok, format_invalid_option_name<option>());
#else
        static_assert(ok, "Invalid option name. You've probably misspelt an option.");
#endif
    }

    /// Get the index of an option and raise an error if the option is not found.
    template <static_string option>
    static constexpr size_t optindex() {
        constexpr size_t sz = optindex_impl<0, option>();
        assert_valid_option_name<(sz < sizeof...(opts)), option>();
        return sz;
    }

    /// Get an option by name.
    // TODO: Use pack indexing once the syntax is fixed and compilers
    // have actually started defining __cpp_pack_indexing.
    template <static_string name>
    using opt_by_name = nth_type<optindex<name>(), opts...>;

    // I hate not having pack indexing.
    template <std::size_t i, static_string str, static_string... strs>
    static constexpr auto nth_str() {
        if constexpr (i == 0) return str;
        else return nth_str<i - 1, strs...>();
    }

    // =======================================================================
    //  Validation.
    // =======================================================================
    static_assert(sizeof...(opts) > 0, "At least one option is required");

    /// Make sure no two options have the same name.
    static consteval bool check_duplicate_options() {
        // State is ok initially.
        bool ok = true;
        std::size_t i = 0;

        // Iterate over each option for each option.
        While<opts...>(ok, [&]<typename opt>() {
            std::size_t j = 0;
            While<opts...>(ok, [&]<typename opt2>() {
                // If the options are not the same, but their names are the same
                // then this is an error. Iteration will stop at this point because
                // \c ok is also the condition for the two loops.
                ok = i == j or opt::name != opt2::name;
                j++;
            });
            i++;
        });

        // Return whether everything is ok.
        return ok;
    }

    // This check is currently broken on MSVC 19.38 and later, for some reason.
#if !defined(_MSC_VER) || defined(__clang__) || _MSC_VER < 1938
    /// Make sure that no option has a prefix that is a short option.
    static consteval bool check_short_opts() {
        // State is ok initially.
        bool ok = true;
        std::size_t i = 0;

        // Iterate over each option for each option.
        While<opts...>(ok, [&]<typename opt>() {
            std::size_t j = 0;
            While<opts...>(ok, [&]<typename opt2>() {
                // Check the condition.
                ok = i == j or not requires { opt::is_short; } or not opt2::name.sv().starts_with(opt::name.sv());
                j++;
            });
            i++;
        });

        // Return whether everything is ok.
        return ok;
    }

    static_assert(check_short_opts(), "Option name may not start with the name of a short option");
#endif

    /// Make sure there is at most one multiple<positional<>> option.
    static consteval size_t validate_multiple() {
        auto is_mul = []<typename opt>() { return requires { opt::is_multiple; }; };
        return (... + (is_mul.template operator()<opts>() and detail::is_positional_v<opts>) );
    }

    template <typename type, static_string... references>
    static consteval bool validate_references_impl(ref<type, references...>) { // clang-format off
        auto ValidateReference = []<static_string str>() {
            return ((
                // Name must reference an existing option.
                opts::name == str and
                // And that option must not also be a ref<> option; this is to
                // prevent cycles.
                not opts::is_ref
            ) or ...);
        };
        return (ValidateReference.template operator()<references>() and ...);
    } // clang-format on

    /// Make sure all referenced values exist.
    static consteval bool validate_references() {
        bool ok = true;
        While<opts...>(ok, [&]<typename opt> {
            using type = typename opt::declared_type_base;
            if constexpr (opt::is_ref) ok = validate_references_impl(type{});
        });
        return ok;
    }

    /// Make sure we don’t have invalid option combinations.
    static_assert(check_duplicate_options(), "Two different options may not have the same name");
    static_assert(validate_multiple() <= 1, "Cannot have more than one multiple<positional<>> option");
    static_assert(validate_references(), "All options with a ref<> type must reference an existing non-ref option");

    // =======================================================================
    //  Option Storage.
    // =======================================================================
    template <typename opt>
    struct storage_type;

    template <typename opt>
    using storage_type_t = typename storage_type<opt>::type;

    template <typename opt>
    using single_element_storage_type_t = remove_vector_t<storage_type_t<opt>>;

    template <typename, typename>
    struct compute_ref_storage_type {
        // Needed so we can instantiate this with an invalid type, even
        // though we never actually use it (yes, there are other ways
        // around this but I can’t be bothered).
        using type = void;
    };

    /// The type used to store a (possibly empty) copy of an option type.
    template <typename opt>
    using ref_storage_type_t = // clang-format off
        // For flags, just store a bool.
        std::conditional_t<is_same<storage_type_t<opt>, bool>, bool,
        // For multiple<> options, store a vector, because we need to deep-copy the state.
        std::conditional_t<is_vector_v<storage_type_t<opt>>, storage_type_t<opt>,
        // Otherwise, store an optional, since the value may be empty.
        std::optional<storage_type_t<opt>>
    >>; // clang-format on

    template <typename declared_type, typename declared_type_base, static_string... args>
    struct compute_ref_storage_type<declared_type, ref<declared_type_base, args...>> { // clang-format off
        using tuple = std::tuple<
            option_type_t<declared_type_base>,
            ref_storage_type_t<opt_by_name<args>>...
        >;

        using type = std::conditional_t<is_vector_v<declared_type>, std::vector<tuple>, tuple>;
    }; // clang-format on

    /// Helper to determine the type used to store an option value.
    ///
    /// This is usually just the canonical type, but for options that
    /// reference other options, we need to add all the references as
    /// well.
    template <typename opt>
    struct storage_type {
        using type = std::conditional_t<
            opt::is_ref,
            compute_ref_storage_type<typename opt::declared_type, typename opt::declared_type_base>,
            std::type_identity<typename opt::canonical_type>
        >::type;
    };

    /// The type returned to the user by 'get<>().
    template <typename opt>
    using get_return_type = // clang-format off
        // For flags, just return a bool.
        std::conditional_t<is_same<typename opt::canonical_type, bool>, bool,
        // For multiple<> options, return a span.
        std::conditional_t<is_vector_v<storage_type_t<opt>>, std::span<single_element_storage_type_t<opt>>,
        // Otherwise, return a pointer.
        storage_type_t<opt>*
    >>; // clang-format on

    /// Various types.
    using help_string_t = static_string<1024 + 1024 * sizeof...(opts)>; // Size should be ‘big enough’™.
    using optvals_tuple_t = std::tuple<storage_type_t<opts>...>;
    using string = std::string;
    using integer = int64_t;

    static constexpr bool has_stop_parsing = (requires { special::is_stop_parsing; } or ...);

public:
    using error_handler_t = std::function<bool(std::string&&)>;

    // =======================================================================
    //  Option Access.
    // =======================================================================
    /// Result type.
    class optvals_type {
        friend clopts_impl;
        optvals_tuple_t optvals{};
        std::bitset<sizeof...(opts)> opts_found{};
        std::conditional_t<has_stop_parsing, std::span<const char*>, empty> unprocessed_args{};

        // This implements get<>() and get_or<>().
        template <static_string s>
        constexpr auto get_impl() -> get_return_type<opt_by_name<s>> {
            using canonical = typename opt_by_name<s>::canonical_type;

            // Bool options don’t have a value. Instead, we just return whether the option was found.
            if constexpr (std::is_same_v<canonical, bool>) return opts_found[optindex<s>()];

            // We always return a span to multiple<> options because the user can just check if it’s empty.
            else if constexpr (detail::is_vector_v<canonical>) return std::get<optindex<s>()>(optvals);

            // Function options don’t have a value.
            else if constexpr (detail::is_callback<canonical>) CLOPTS_ERR("Cannot call get<>() on an option with function type.");

            // Otherwise, return nullptr if the option wasn’t found, and a pointer to the value otherwise.
            else return not opts_found[optindex<s>()] ? nullptr : std::addressof(std::get<optindex<s>()>(optvals));
        }

    public:
        /// \brief Get the value of an option.
        ///
        /// This is not \c [[nodiscard]] because that raises an ICE when compiling
        /// with some older versions of GCC.
        ///
        /// \return \c true / \c false if the option is a flag
        /// \return \c nullptr if the option was not found
        /// \return a pointer to the value if the option was found
        ///
        /// \see get_or()
        template <static_string s>
        constexpr auto get() {
            // Check if the option exists before calling get_impl<>() so we trigger the static_assert
            // below before hitting a complex template instantiation error.
            constexpr auto sz = optindex_impl<0, s>();
            if constexpr (sz < sizeof...(opts)) return get_impl<s>();
            else assert_valid_option_name<(sz < sizeof...(opts)), s>();
        }

        /// \brief Get the value of an option or a default value if the option was not found.
        ///
        /// The default value is \c static_cast to the type of the option value.
        ///
        /// \param default_ The default value to return if the option was not found.
        /// \return \c default_ if the option was not found.
        /// \return a copy of the option value if the option was found.
        ///
        /// \see get()
        template <static_string s>
        constexpr auto get_or(auto default_) {
            constexpr auto sz = optindex_impl<0, s>();
            if constexpr (sz < sizeof...(opts)) {
                if (opts_found[optindex<s>()]) return *get_impl<s>();
                return static_cast<std::remove_cvref_t<decltype(*get_impl<s>())>>(default_);
            } else {
                assert_valid_option_name<(sz < sizeof...(opts)), s>();
            }
        }

        /// \brief Get unprocessed options.
        ///
        /// If the \c stop_parsing\<> option was encountered, this will return the
        /// remaining options that have not been processed by this parser. If there
        /// is no \c stop_parsing\<> option, this will always return an empty span.
        ///
        /// If there was an error during parsing, the return value of this function
        /// is unspecified.
        [[nodiscard]] auto unprocessed() const -> std::span<const char*> {
            if constexpr (has_stop_parsing) return unprocessed_args;
            else return {};
        }
    };

private:
    // =======================================================================
    //  Parser State.
    // =======================================================================
    /// Variables for the parser and for storing parsed options.
    optvals_type optvals{};
    bool has_error = false;
    int argc{};
    int argi{};
    const char** argv{};
    void* user_data{};
    error_handler_t error_handler{};

    // =======================================================================
    //  Helpers.
    // =======================================================================
    /// Error handler that is used if the user doesn’t provide one.
    bool default_error_handler(std::string&& errmsg) {
        auto name = program_name();
        if (not name.empty()) std::cerr << name << ": ";
        std::cerr << errmsg << "\n";

        // Invoke the help option.
        bool invoked = false;
        auto invoke = [&]<typename opt> {
            if constexpr (requires { opt::is_help_option; }) {
                invoked = true;
                invoke_help_callback<opt>();
            }
        };

        // If there is a help option, invoke it.
        (invoke.template operator()<opts>(), ...);

        // If no help option was found, print the help message.
        if (not invoked) {
            std::cerr << "Usage: ";
            if (not name.empty()) std::cerr << name << " ";
            std::cerr << help();
        }

        std::exit(1);
    }

    /// Invoke the error handler and set the error flag.
    void handle_error(auto first, auto&&... msg_parts) {
        // Append the message parts.
        std::string msg = std::string{std::move(first)};
        ((msg += std::forward<decltype(msg_parts)>(msg_parts)), ...);

        // Dispatch the error.
        has_error = not error_handler(std::move(msg));
    }

    /// Invoke the help callback of the help option.
    template <typename opt>
    void invoke_help_callback() {
        // New API: program name + help message [+ user data].
        using sv = std::string_view;
        if constexpr (requires { opt::help_callback(sv{}, sv{}, user_data); })
            opt::help_callback(sv{program_name()}, sv{}, user_data);
        else if constexpr (requires { opt::help_callback(sv{}, sv{}); })
            opt::help_callback(sv{program_name()}, help_message_raw.sv());

        // Compatibility for callbacks that don’t take the program name.
        else if constexpr (requires { opt::help_callback(sv{}, user_data); })
            opt::help_callback(help_message_raw.sv(), user_data);
        else if constexpr (requires { opt::help_callback(sv{}); })
            opt::help_callback(help_message_raw.sv());

        // Invalid help option callback.
        else static_assert(
            detail::always_false<opt>,
            "Invalid help option signature. Consult the README for more information"
        );
    }

    /// Helper to parse an integer or double.
    template <typename number_type, static_string name>
    auto parse_number(std::string_view s, auto parse_func) -> number_type {
        number_type i{};

        // The empty string is a valid integer *and* float, apparently.
        if (s.empty()) {
            handle_error("Expected ", name.sv(), ", got empty string");
            return i;
        }

        // Parse the number.
        errno = 0;
        char* pos{};
        if constexpr (requires { parse_func(s.data(), &pos, 10); }) i = number_type(parse_func(s.data(), &pos, 10));
        else i = number_type(parse_func(s.data(), &pos));
        if (errno != 0 or *pos) handle_error(s, " does not appear to be a valid ", name.sv());
        return i;
    }

    /// Get the program name, if available.
    auto program_name() const -> std::string_view {
        if (argv) return argv[0];
        return {};
    }

    // =======================================================================
    //  Internal Option Access.
    // =======================================================================
    /// Check if an option was found.
    template <static_string option>
    bool found() { return optvals.opts_found[optindex<option>()]; }

    /// Get a reference to an option value.
    template <static_string s>
    [[nodiscard]] constexpr auto ref_to_storage() -> decltype(std::get<optindex<s>()>(optvals.optvals))& {
        using value_type = decltype(std::get<optindex<s>()>(optvals.optvals));

        // Bool options don’t have a value.
        if constexpr (std::is_same_v<value_type, bool>) CLOPTS_ERR("Cannot call ref() on an option<bool>");

        // Function options don’t have a value.
        else if constexpr (detail::is_callback<value_type>) CLOPTS_ERR("Cannot call ref<>() on an option with function type.");

        // Get the option value.
        else return std::get<optindex<s>()>(optvals.optvals);
    }

    /// Mark an option as found.
    template <static_string option>
    void set_found() { optvals.opts_found[optindex<option>()] = true; }

    /// Store an option value.
    template <bool is_multiple>
    void store_option_value(auto& ref, auto value) {
        // Set the value.
        if constexpr (is_multiple) ref.push_back(std::move(value));
        else ref = std::move(value);
    }

    // =======================================================================
    //  Help Message.
    // =======================================================================
    /// Create the help message.
    static constexpr auto make_help_message() -> help_string_t { // clang-format off
        using positional_unsorted = filter<is_positional, opts...>;
        using positional = sort<get_option_name, positional_unsorted>;
        using non_positional = sort<get_option_name, filter<is_not_positional, opts...>>;
        using values_opts = sort<get_option_name, filter<is_values_option, opts...>>;
        help_string_t msg{};

        // Append the positional options.
        //
        // Do NOT sort them here as this is where we print in what order
        // they’re supposed to appear in, so sorting would be very stupid
        // here.
        bool have_positional_opts = false;
        positional_unsorted::each([&]<typename opt> {
            have_positional_opts = true;
            if (not opt::is_required) msg.append("[");
            msg.append("<");
            msg.append(opt::name.arr, opt::name.len);
            msg.append(">");
            if (not opt::is_required) msg.append("]");
            msg.append(" ");
        });

        // End of first line.
        msg.append("[options]\n");

        // Determine the length of the longest name + typename so that
        // we know how much padding to insert before actually printing
        // the description. Also factor in the <> signs around and the
        // space after the option name, as well as the type name.
        size_t max_vals_opt_name_len{};
        size_t max_len{};
        Foreach<opts...>([&]<typename opt> {
            if constexpr (opt::is_values)
                max_vals_opt_name_len = std::max(max_vals_opt_name_len, opt::name.len);

            // If we’re printing the type, we have the following formats:
            //
            //     name <type>    Description
            //     <name> : type  Description
            //
            // Apart from the type name, we also need to account for the extra
            // ' <>' of normal options, and for the extra '<>' as well as the
            // ' : ' of positional options.
            if constexpr (should_print_argument_type<opt>) {
                auto n = type_name<typename opt::canonical_type>();
                max_len = std::max(
                    max_len,
                    opt::name.len + n.len + (is_positional_v<opt> ? 5 : 3)
                );
            }

            // Otherwise, we only care about the name of the option and the
            // extra '<>' of positional options.
            else {
                if constexpr (is_positional_v<opt>) max_len = std::max(max_len, opt::name.len + 2);
                else max_len = std::max(max_len, opt::name.len);
            }
        });

        // Append an argument.
        auto append = [&]<typename opt> {
            msg.append("    ");
            auto old_len = msg.size();

            // Append name.
            if constexpr (is_positional_v<opt>) msg.append("<");
            msg.append(opt::name.arr, opt::name.len);
            if constexpr (is_positional_v<opt>) msg.append(">");

            // Append type.
            if constexpr (should_print_argument_type<opt>) {
                auto tname = type_name<typename opt::canonical_type>();
                msg.append(" : ");
                msg.append(tname.arr, tname.len);
            }

            // Align to right margin.
            auto len = msg.size() - old_len;
            for (size_t i = 0; i < max_len - len; i++) msg.append(" ");

            // Two extra spaces between this and the description.
            msg.append("  ");
            msg.append(opt::description.arr, opt::description.len);
            msg.append("\n");
        };

        // Append the descriptions of positional options.
        if (have_positional_opts) {
            msg.append("\nArguments:\n");
            positional::each(append);
            msg.append("\n");
        }

        // Append non-positional options.
        msg.append("Options:\n");
        non_positional::each(append);

        // If we have any values<> types, print their supported values.
        if constexpr ((opts::is_values or ...)) {
            msg.append("\nSupported option values:\n");
            values_opts::each([&] <typename opt> {
                if constexpr (opt::is_values) {
                    msg.append("    ");
                    msg.append(opt::name.arr, opt::name.len);
                    msg.append(":");

                    // Padding after the name.
                    for (size_t i = 0; i < max_vals_opt_name_len - opt::name.len + 1; i++)
                        msg.append(" ");

                    // Option values.
                    msg.len += opt::print_values(msg.arr + msg.len);
                    msg.append("\n");
                }
            });
        }

        // Return the combined help message.
        return msg;
    } // clang-format on

    /// Help message is created at compile time.
    static constexpr help_string_t help_message_raw = make_help_message();

public:
    /// Get the help message.
    static auto help() -> std::string {
        return {help_message_raw.arr, help_message_raw.len};
    }

private:
    // =======================================================================
    //  References.
    // =======================================================================
    /// Add a referenced option to a tuple.
    template <std::size_t index, static_string name>
    void add_referenced_option(auto& tuple) {
        // +1 here because the first index is the actual option value.
        auto& storage = std::get<index + 1>(tuple);
        if (found<name>()) {
            using opt = opt_by_name<name>;
            if constexpr (opt::is_flag) storage = true;
            else if constexpr (is_vector_v<storage_type_t<opt>>) storage = ref_to_storage<name>();
            else storage = std::make_optional(*optvals.template get<name>());
        }
    }

    /// Add all referenced options to a tuple.
    template <typename type, static_string... args>
    auto add_referenced_options(auto& tuple, ref<type, args...>) {
        [&]<std::size_t... i>(std::index_sequence<i...>) {
            (add_referenced_option<i, nth_str<i, args...>()>(tuple), ...);
        }(std::make_index_sequence<sizeof...(args)>());
    }

    /// Collect all references referenced by an optio.
    template <typename opt>
    auto collect_references(auto value) {
        using tuple_ty = single_element_storage_type_t<opt>;
        tuple_ty tuple;
        std::get<0>(tuple) = std::move(value);
        add_referenced_options(tuple, typename opt::declared_type_base{});
        return tuple;
    }

    // =======================================================================
    //  Parsing and Dispatch.
    // =======================================================================
    /// Handle an option value.
    template <typename opt, bool is_multiple>
    void dispatch_option_with_arg(std::string_view opt_str, std::string_view opt_val) {
        using canonical = typename opt::canonical_type;

        // Mark the option as found.
        set_found<opt::name>();

        // If this is a function option, simply call the callback and we're done.
        if constexpr (detail::is_callback<canonical>) {
            if constexpr (detail::is<canonical, callback_noarg_type>) opt::callback(user_data, opt_str);
            else opt::callback(user_data, opt_str, opt_val);
        }

        // Otherwise, parse the argument.
        else {
            // Create the argument value.
            auto value = make_arg<opt>(opt_val);

            // If this option takes a list of values, check that the
            // value matches one of them.
            if constexpr (opt::is_values) {
                if (not opt::is_valid_option_value(value)) {
                    handle_error(
                        "Invalid value for option '",
                        std::string(opt_str),
                        "': '",
                        std::string(opt_val),
                        "'"
                    );
                }
            }

            // If this is a ref<> option, remember to unwrap it first.
            auto& storage = ref_to_storage<opt::name>();
            if constexpr (opt::is_ref) {
                store_option_value<is_multiple>(
                    storage,
                    collect_references<opt>(std::move(value))
                );
            } else {
                store_option_value<is_multiple>(storage, std::move(value));
            }
        }
    }

    /// Handle an option that may take an argument.
    ///
    /// Both --option value and --option=value are valid ways of supplying a
    /// value. We test for both of them.
    template <typename opt, bool is_multiple>
    bool handle_opt_with_arg(std::string_view opt_str) {
        using canonical = typename opt::canonical_type;

        // --option=value or short opt.
        if (opt_str.size() > opt::name.len) {
            // Parse the rest of the option as the value if we have a '=' or if this is a short option.
            if (opt_str[opt::name.len] == '=' or requires { opt::is_short; }) {
                // Otherwise, parse the value.
                auto opt_start_offs = opt::name.len + (opt_str[opt::name.len] == '=');
                const auto opt_name = opt_str.substr(0, opt_start_offs);
                const auto opt_val = opt_str.substr(opt_start_offs);
                dispatch_option_with_arg<opt, is_multiple>(opt_name, opt_val);
                return true;
            }

            // Otherwise, this isn’t the right option.
            return false;
        }

        // Handle the option. If we get here, we know that the option name that we’ve
        // encountered matches the option name exactly. If this is a func option that
        // doesn’t take arguments, just call the callback and we’re done.
        if constexpr (detail::is<canonical, callback_noarg_type>) {
            opt::callback(user_data, opt_str);
            return true;
        }

        // Otherwise, try to consume the next argument as the option value.
        else {
            // No more command line arguments left.
            if (++argi == argc) {
                handle_error("Missing argument for option \"", opt_str, "\"");
                return false;
            }

            // Parse the argument.
            dispatch_option_with_arg<opt, is_multiple>(opt_str, argv[argi]);
            return true;
        }
    }

    /// Handle an option. The parser calls this on each non-positional option.
    template <typename opt>
    bool handle_regular_impl(std::string_view opt_str) {
        // If the supplied string doesn’t start with the option name, move on to the next option
        if (not opt_str.starts_with(opt::name.sv())) return false;

        // Check if this option accepts multiple values.
        using element = typename opt::single_element_type;
        static constexpr bool is_multiple = requires { opt::is_multiple; };
        if constexpr (not is_multiple and not detail::is_callback<element>) {
            // Duplicate options are not allowed, unless they’re overridable.
            if (not opt::is_overridable and found<opt::name>()) {
                std::string errmsg;
                errmsg += "Duplicate option: \"";
                errmsg += opt_str;
                errmsg += "\"";
                handle_error(std::move(errmsg));
                return false;
            }
        }

        // Flags and callbacks that don't have arguments.
        if constexpr (not detail::has_argument<element>) {
            // Check if the name of this flag matches the entire option string that
            // we encountered. If we’re just a prefix, then we don’t handle this.
            if (opt_str != opt::name.sv()) return false;

            // Mark the option as found. That’s all we need to do for flags.
            set_found<opt::name>();

            // If it’s a callable, call it.
            if constexpr (detail::is_callback<element>) {
                // The builtin help option is handled here. We pass the help message as an argument.
                if constexpr (requires { opt::is_help_option; }) invoke_help_callback<opt>();

                // If it’s not the help option, just invoke it.
                else { opt::callback(user_data, opt_str); }
            }

            // Option has been handled.
            return true;
        }

        // Handle an option that may take an argument.
        else { return handle_opt_with_arg<opt, is_multiple>(opt_str); }
    }

    /// Handle a positional option.
    template <typename opt>
    bool handle_positional_impl(std::string_view opt_str) {
        static_assert(not detail::is_callback<typename opt::canonical_type>, "positional<>s may not have a callback");

        // If we've already encountered this positional option, then return.
        static constexpr bool is_multiple = requires { opt::is_multiple; };
        if constexpr (not is_multiple) {
            if (found<opt::name>()) return false;
        }

        // Otherwise, attempt to parse this as the option value.
        dispatch_option_with_arg<opt, is_multiple>(opt::name.sv(), opt_str);
        return true;
    }

    /// Invoke handle_regular_impl on every option until one returns true.
    bool handle_regular(std::string_view opt_str) {
        const auto handle = [this]<typename opt>(std::string_view str) {
            // `this->` is to silence a warning.
            if constexpr (detail::is_positional_v<opt>) return false;
            else return this->handle_regular_impl<opt>(str);
        };

        return (handle.template operator()<opts>(opt_str) or ...);
    }

    /// Invoke handle_positional_impl on every option until one returns true.
    bool handle_positional(std::string_view opt_str) {
        const auto handle = [this]<typename opt>(std::string_view str) {
            // `this->` is to silence a warning.
            if constexpr (detail::is_positional_v<opt>) return this->handle_positional_impl<opt>(str);
            else return false;
        };

        return (handle.template operator()<opts>(opt_str) or ...);
    }

    /// Parse an option value.
    template <typename opt>
    auto make_arg(std::string_view opt_val) -> typename opt::single_element_type {
        using element = typename opt::single_element_type;

        // Make sure this option takes an argument.
        if constexpr (not detail::has_argument<element>) CLOPTS_ERR("This option type does not take an argument");

        // Strings do not require parsing.
        else if constexpr (std::is_same_v<element, std::string>) return std::string{opt_val};

        // If it’s a file, read its contents.
        else if constexpr (requires { element::is_file_data; }) return detail::map_file<element>(opt_val, error_handler);

        // Parse an integer or double.
        else if constexpr (std::is_same_v<element, integer>) return parse_number<integer, "integer">(opt_val, std::strtoull);
        else if constexpr (std::is_same_v<element, double>) return parse_number<double, "floating-point number">(opt_val, std::strtod);

        // Should never get here.
        else CLOPTS_ERR("Unreachable");
    }

    /// Check if we should stop parsing.
    template <typename opt>
    bool stop_parsing(std::string_view opt_str) {
        if constexpr (requires { opt::is_stop_parsing; }) return opt_str == opt::name.sv();
        return false;
    }

    void parse() {
        // Main parser loop.
        for (argi = 1; argi < argc; argi++) {
            std::string_view opt_str{argv[argi]};

            // Stop parsing if this is the stop_parsing<> option.
            if ((stop_parsing<special>(opt_str) or ...)) {
                argi++;
                break;
            }

            // Attempt to handle the option.
            if (not handle_regular(opt_str) and not handle_positional(opt_str)) {
                std::string errmsg;
                errmsg += "Unrecognized option: \"";
                errmsg += opt_str;
                errmsg += "\"";
                handle_error(std::move(errmsg));
            }

            // Stop parsing if there was an error.
            if (has_error) return;
        }

        // Make sure all required options were found.
        Foreach<opts...>([&]<typename opt>() {
            if (not found<opt::name>() and opt::is_required) {
                std::string errmsg;
                errmsg += "Option \"";
                errmsg += opt::name.sv();
                errmsg += "\" is required";
                handle_error(std::move(errmsg));
            }
        });

        // Save unprocessed options.
        if constexpr (has_stop_parsing) {
            optvals.unprocessed_args = std::span<const char*>{
                argv + argi,
                static_cast<std::size_t>(argc - argi),
            };
        }
    }

public:
    /// \brief Parse command line options.
    ///
    /// \param argc The argument count.
    /// \param argv The arguments (including the program name).
    /// \param user_data User data passed to any func\<\> options that accept a \c void*.
    /// \param error_handler A callback that is invoked whenever an error occurs. If
    ///        \c nullptr is passed, the default error handler is used. The error handler
    ///        should return \c true if parsing should continue and \c false otherwise.
    /// \return The parsed option values.
    static auto parse(
        int argc,
        const char* const* const argv,
        std::function<bool(std::string&&)> error_handler = nullptr,
        void* user_data = nullptr
    ) -> optvals_type {
        // Initialise state.
        clopts_impl self;
        if (error_handler) self.error_handler = error_handler;
        else self.error_handler = [&](auto&& e) { return self.default_error_handler(std::forward<decltype(e)>(e)); };
        self.argc = argc;
        self.user_data = user_data;

        // Safe because we don’t attempt to modify argv anyway. This
        // is just so we can pass in both e.g. a `const char**` and a
        // `char **`.
        self.argv = const_cast<const char**>(argv);

        // Parse the options.
        self.parse();
        return std::move(self.optvals);
    }
};

} // namespace detail

/// ===========================================================================
///  API
/// ===========================================================================
/// Main command-line options type.
template <typename... opts>
using clopts = detail::clopts_impl< // clang-format off
    detail::filter<detail::regular_option, opts...>,
    detail::filter<detail::special_option, opts...>
>; // clang-format on

/// Types.
using detail::ref;
using detail::values;

/// Base option type.
template <
    detail::static_string _name,
    detail::static_string _description = "",
    typename type = std::string,
    bool required = false,
    bool overridable = false>
struct option : detail::opt_impl<_name, _description, type, required, overridable> {};

/// Identical to 'option', but overridable by default.
template <
    detail::static_string _name,
    detail::static_string _description,
    typename type = std::string,
    bool required = false>
struct overridable : option<_name, _description, type, required, true> {};

namespace experimental {
/// Base short option type.
template <
    detail::static_string _name,
    detail::static_string _description = "",
    typename _type = std::string,
    bool required = false,
    bool overridable = false>
struct short_option : detail::opt_impl<_name, _description, _type, required, overridable> {
    static constexpr decltype(_name) name = _name;
    static constexpr decltype(_description) description = _description;
    static constexpr bool is_flag = std::is_same_v<_type, bool>;
    static constexpr bool is_required = required;
    static constexpr bool is_short = true;
    static constexpr bool option_tag = true;

    constexpr short_option() = delete;
};
} // namespace experimental

/// A file.
template <typename contents_type_t = std::string, typename path_type_t = std::filesystem::path>
struct file {
    using contents_type = contents_type_t;
    using path_type = path_type_t;
    using element_type = typename contents_type::value_type;
    using element_pointer = std::add_pointer_t<element_type>;
    static constexpr bool is_file_data = true;

    /// The file path.
    path_type path;

    /// The contents of the file.
    contents_type contents;
};

/// For backwards compatibility.
using file_data = file<>;

/// A positional option.
///
/// Positional options cannot be overridable; use multiple<positional<>>
/// instead.
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
    static constexpr typename lambda::lambda callback = {};
};

/// A function option.
template <
    detail::static_string _name,
    detail::static_string _description,
    auto cb,
    bool required = false>
struct func : func_impl<_name, _description, detail::make_lambda<cb>, required> {};

/// A flag option.
///
/// Flags are never required because that wouldn’t make much sense.
template <
    detail::static_string _name,
    detail::static_string _description = "">
struct flag : option<_name, _description, bool> {};

/// The help option.
template <auto _help_cb = detail::default_help_handler>
struct help : func<"--help", "Print this help information", [] {}> {
    static constexpr decltype(_help_cb) help_callback = _help_cb;
    static constexpr bool is_help_option = true;
};

/// Multiple meta-option.
template <typename opt>
struct multiple : option<opt::name, opt::description, std::vector<typename opt::declared_type>, opt::is_required> {
    using base_type = typename opt::canonical_type;
    using type = std::vector<typename opt::canonical_type>;
    static_assert(not detail::is<base_type, bool>, "Type of multiple<> cannot be bool");
    static_assert(not detail::is<base_type, detail::callback_arg_type>, "Type of multiple<> cannot be a callback");
    static_assert(not detail::is<base_type, detail::callback_noarg_type>, "Type of multiple<> cannot be a callback");
    static_assert(not requires { opt::is_multiple; }, "multiple<multiple<>> is invalid");
    static_assert(not requires { opt::is_stop_parsing; }, "multiple<stop_parsing<>> is invalid");
    static_assert(not opt::is_overridable, "multiple<> cannot be overridable");

    constexpr multiple() = delete;
    static constexpr bool is_multiple = true;
    using is_positional_ = detail::positional_t<opt>;
};

/// Stop parsing when this option is encountered.
template <detail::static_string stop_at = "--">
struct stop_parsing : option<stop_at, "Stop parsing command-line arguments", detail::special_tag> {
    static constexpr bool is_stop_parsing = true;
};

} // namespace command_line_options

#undef CLOPTS_STRLEN
#undef CLOPTS_STRCMP
#undef CLOPTS_ERR
#endif // CLOPTS_H
