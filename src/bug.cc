#include <algorithm>
#include <type_traits>
#include <string>

template <size_t sz>
struct str_lit {
    char data[sz]{};
    constexpr str_lit(const char (&_data)[sz]) { std::copy_n(_data, sz, data); }
    template <typename str>
    constexpr bool operator==(const str& s) const {
        return sizeof data == sizeof s.data && __builtin_strcmp(data, s.data) == 0;
    }
};

template <str_lit _name, typename _type = std::string>
struct option {
    using type                                     = _type;
    static constexpr inline decltype(_name) name = _name;
};

template <typename... opts>
struct command_line_options {
    template <str_lit s, typename... options> struct type_of;

    template <str_lit s, typename option, typename option2>
    struct type_of<s, option, option2> {
        using type = std::conditional_t<s == option::name,
            typename option::type,
            typename option2::type>;
    };

    template <str_lit s, typename option, typename... options>
    struct type_of<s, option, options...> {
        using type = std::conditional_t<sizeof...(options) == 0 || s == option::name,
            typename option::type,
            typename type_of<s, options...>::type>;
    };

    template <str_lit s>
    using type_of_t = typename type_of<s, opts...>::type;

    struct parsed_options {
        template <str_lit s>
        [[gnu::warn_unused_result]] type_of_t<s> get() { return {}; }
    };
};

using options = command_line_options<
    option<"--filename">,
    option<"--size", int64_t>>;

int main() {
    options::parsed_options opts;
    opts.get<"--filename">();
    // Doing this instead works: (void) opts.get<"--filename">();
}