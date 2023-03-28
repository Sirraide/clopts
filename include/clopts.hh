#ifndef CLOPTS_H
#define CLOPTS_H
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <variant>
#include <filesystem>

#ifndef RAISE_COMPILE_ERROR
#define RAISE_COMPILE_ERROR(msg)                  \
    []<bool x = false> { static_assert(x, msg); } \
    ()
#endif

namespace command_line_options {

/// A file.
template <typename contents_type_t = std::string>
struct file {
    using contents_type = contents_type_t;
    using element_type = typename contents_type::value_type;
    using element_pointer = std::add_pointer_t<element_type>;
    static constexpr bool is_file_data = true;

    std::filesystem::path path;
    contents_type contents;
};

using file_data = file<>;

template <size_t sz>
struct static_string {
    char data[sz]{};
    size_t len{};

    constexpr static_string() {}
    constexpr static_string(const char (&_data)[sz]) {
        std::copy_n(_data, sz, data);
        len = sz - 1;
    }

    template <typename str>
    [[nodiscard]] constexpr bool operator==(const str& s) const { return len == s.len && __builtin_strcmp(data, s.data) == 0; }

    template <size_t n>
    constexpr void operator+=(const static_string<n>& str) {
        static_assert(len + str.len < sz, "Cannot append string because it is too long");
        std::copy_n(str.data, str.len, data + len);
        len += str.len;
    }

    constexpr void append(const char* str) { append(str, __builtin_strlen(str)); }
    constexpr void append(const char* str, size_t length) {
        std::copy_n(str, length, data + len);
        len += length;
    }
    [[nodiscard]] constexpr auto c_str() const -> const char* { return data; }
    [[nodiscard]] constexpr auto size() const -> size_t { return len; }
    [[nodiscard]] constexpr auto sv() const -> std::string_view { return {data, len}; }
};

/// This is a template just so that the compiler merges multiple definitions.
template <bool = true>
void print_help_and_exit(void* msg) {
    std::cerr << *reinterpret_cast<std::string*>(msg);
    std::exit(0);
}

template <typename t>
constexpr inline bool is_vector_v = std::is_same_v<t, std::vector<std::string>> //
                                    || std::is_same_v<t, std::vector<double>>   //
                                    || std::is_same_v<t, std::vector<int64_t>>  //
                                    || std::is_same_v<t, std::vector<file_data>>
                                    || std::is_same_v<t, std::vector<file<std::vector<char>>>>;

template <typename t>
struct base_type_type;

template <typename t>
struct base_type_type<std::vector<t>> {
    using type = t;
};

template <typename t>
struct base_type_type {
    using type = t;
};

template <typename t>
using base_type_t = typename base_type_type<t>::type;

/// Check if an option is a positional option.
template <typename opt>
struct is_positional {
    enum { value = requires { {typename opt::is_positional_{} } -> std::same_as<std::true_type>; } };
    using type = std::bool_constant<value>;
};

template <typename opt>
using positional_t = typename is_positional<opt>::type;

template <typename opt>
inline constexpr bool is_positional_v = is_positional<opt>::value;

template <static_string _name, static_string _description = "", typename _type = std::string, bool required = false>
struct option {
    static_assert(sizeof _description.data < 512, "Description may not be longer than 512 characters");
    static_assert(_name.len > 0, "Option name may not be empty");
    static_assert(sizeof _name.data < 256, "Option name may not be longer than 256 characters");
    static_assert(!std::is_void_v<_type>, "Option type may not be void. Use bool instead");
    static_assert(std::is_same_v<_type, std::string>            //
                      || std::is_same_v<_type, bool>            //
                      || std::is_same_v<_type, double>          //
                      || std::is_same_v<_type, int64_t>         //
                      || requires { _type::is_file_data; }      //
                      || std::is_same_v<_type, void (*)(void*)> //
                      || is_vector_v<_type>,                    //
        "Option type must be std::string, bool, int64_t, double, file_data, or void(*)(), or a vector thereof");

    using type = _type;
    static constexpr inline decltype(_name) name = _name;
    static constexpr inline decltype(_description) description = _description;
    static constexpr inline bool is_flag = std::is_same_v<_type, bool>;
    static constexpr inline bool is_required = required;

    constexpr option() = delete;
};

template <static_string _name, static_string _description, typename _type = std::string, bool required = true>
struct positional : option<_name, _description, _type, required> {
    using is_positional_ = std::true_type;
};

template <static_string _name, static_string _description, void (*f)(void*), void* arg = nullptr, bool required = false>
struct func : public option<_name, _description, void (*)(void*), required> {
    static constexpr inline decltype(f) callback = f;
    static inline void* argument = arg;

    constexpr func() = delete;
};

template <static_string _name, static_string _description = "", bool required = false>
struct flag : public option<_name, _description, bool, required> {
    constexpr flag() = delete;
};

struct help : public func<"--help", "Print this help information", print_help_and_exit> {
    constexpr help() = delete;
    static constexpr inline bool is_help_option = true;
};

template <typename opt>
struct multiple : public option<opt::name, opt::description, std::vector<typename opt::type>, opt::is_required> {
    using base_type = typename opt::type;
    using type = std::vector<typename opt::type>;
    static_assert(!std::is_same_v<base_type, bool>, "Type of multiple<> cannot be bool");
    static_assert(!std::is_same_v<base_type, void (*)(void*)>, "Type of multiple<> cannot be void(*)(void*)");

    constexpr multiple() = delete;
    static constexpr inline bool is_multiple = true;
    using is_positional_ = positional_t<opt>;
};

template <typename... opts>
struct clopts {
protected:
    constexpr clopts() = delete;
    constexpr ~clopts() = delete;
    clopts(const clopts& o) = delete;
    clopts(clopts&& o) = delete;
    clopts& operator=(const clopts& o) = delete;
    clopts& operator=(clopts&& o) = delete;

    /// Make sure no two options have the same name.
    static constexpr bool check_duplicate_options() { // clang-format off
        bool ok = true;
        size_t i{};
        ([&](const char *a, size_t size_a) {
            size_t j{};
            ([&](const char *b, size_t size_b) {
                if (ok) ok = size_a != size_b || __builtin_strcmp(a, b) != 0 || i == j;
                j++;
            }(opts::name.data, opts::name.len), ...);
            i++;
        }(opts::name.data, opts::name.len), ...);
        return ok;
    } // clang-format on

    static constexpr size_t validate_multiple() { // clang-format off
        return (... + (requires { opts::is_multiple; } && is_positional_v<opts> ? 1 : 0));
    } // clang-format on

    static_assert(check_duplicate_options(), "Two different options may not have the same name");
    static_assert(validate_multiple() <= 1, "Cannot have more than one multiple<positional<>> option");

    using help_string_t = static_string<1024 * sizeof...(opts)>;
    using string = std::string;
    using integer = int64_t;
    using callback = void (*)(void*);

    static inline bool has_error;
    static inline int argc;
    static inline int argi;
    static inline char** argv;
    static inline std::tuple<typename opts::type...> optvals;
    static inline std::array<bool, sizeof...(opts)> opts_found{};
    static constexpr inline std::array<const char*, sizeof...(opts)> opt_names{opts::name.data...};
    static inline bool opts_parsed{};

    template <size_t index, static_string option>
    static constexpr size_t optindex_impl() {
        if constexpr (index >= sizeof...(opts)) return index;
        else if constexpr (__builtin_strcmp(opt_names[index], option.data) == 0) return index;
        else return optindex_impl<index + 1, option>();
    }

    template <static_string option>
    static constexpr size_t optindex() {
        constexpr size_t sz = optindex_impl<0, option>();
        static_assert(sz < sizeof...(opts), "Invalid option name. You've probably misspelt an option.");
        return sz;
    }

    template <static_string option>
    static constexpr bool found() {
        return opts_found[optindex<option>()];
    }

    template <static_string option>
    static constexpr void set_found() {
        opts_found[optindex<option>()] = true;
    }

    template <static_string s>
    [[nodiscard]] static constexpr auto ref() -> decltype(std::get<optindex<s>()>(optvals))& {
        using value_type = decltype(std::get<optindex<s>()>(optvals));
        if constexpr (std::is_same_v<value_type, bool>) RAISE_COMPILE_ERROR("Cannot call ref() on an option<bool>");
        else if constexpr (std::is_same_v<value_type, callback> || std::is_same_v<value_type, std::vector<callback>>)
            RAISE_COMPILE_ERROR("Cannot call get<>() on an option with function type.");
        else {
            return std::get<optindex<s>()>(optvals);
        }
    }

    template <static_string s>
    using optval_t = std::remove_cvref_t<decltype(std::get<optindex<s>()>(optvals))>;

    template <static_string s>
    static constexpr auto get_impl() -> std::conditional_t<std::is_same_v<optval_t<s>, bool>, bool, optval_t<s>*> {
        using value_type = optval_t<s>;
        if constexpr (std::is_same_v<value_type, bool>) return opts_found[optindex<s>()];
        else if constexpr (std::is_same_v<value_type, callback> || std::is_same_v<value_type, std::vector<callback>>)
            RAISE_COMPILE_ERROR("Cannot call get<>() on an option with function type.");
        else {
            if (!opts_found[optindex<s>()]) return nullptr;
            return std::addressof(std::get<optindex<s>()>(optvals));
        }
    }

public:
    /// Get the value of an option.
    ///
    /// \return true/false if the option is a flag
    /// \return nullptr if the option was not found
    /// \return a pointer to the value if the option was found
    template <static_string s>
    static constexpr auto get() {
        constexpr auto sz = optindex_impl<0, s>();
        if constexpr (sz < sizeof...(opts)) return get_impl<s>();
        else static_assert(sz < sizeof...(opts), "Invalid option name. You've probably misspelt an option.");
    }

    protected:
    static constexpr auto make_help_message() -> help_string_t {
        help_string_t msg{};

        /// Determine the length of the longest name + typename.
        size_t max_len{};
        auto determine_max_len = [&]<typename opt> {
            if constexpr (is_positional_v<opt>) return;
            size_t combined_len = opt::name.len;
            if constexpr (!opt::is_flag && !std::is_same_v<typename opt::type, callback>) combined_len += (type_name<typename opt::type>().len + 3);
            if (combined_len > max_len) max_len = combined_len;
        };
        (determine_max_len.template operator()<opts>(), ...);

        /// Append the positional options.
        msg.append(" ");
        auto append_positional_options = [&]<typename opt> {
            if constexpr (not is_positional_v<opt>) return;
            msg.append("<");
            msg.append(opt::name.data, opt::name.len);
            msg.append("> ");
        };
        (append_positional_options.template operator()<opts>(), ...);
        msg.append("[options]\n");

        /// Append the options
        msg.append("Options:\n");
        auto append_option = [&]<typename opt> {
            /// If this is a positional option, we don't want to append it here.
            if constexpr (is_positional_v<opt>) return;

            /// Compute the padding for this option.
            const auto tname = type_name<typename opt::type>();
            size_t len = opt::name.len;
            if constexpr (!opt::is_flag && !std::is_same_v<typename opt::type, callback>) len += (3 + tname.len);
            size_t padding = max_len - len;
            /// Append the name and the type arg if this is not a bool option.
            msg.append("    ");
            msg.append(opt::name.data, opt::name.len);
            if constexpr (!opt::is_flag && !std::is_same_v<typename opt::type, callback>) {
                msg.append(" <");
                msg.append(tname.data, tname.len);
                msg.append(">");
            }
            /// Append the padding.
            for (size_t i = 0; i < padding; i++) msg.append(" ");
            msg.append("  ");
            /// Append the description.
            msg.append(opt::description.data, opt::description.len);
            msg.append("\n");
        };
        (append_option.template operator()<opts>(), ...);

        return msg;
    };
    static constexpr inline help_string_t help_message_raw = make_help_message();

    static auto help() -> std::string {
        std::string msg = "Usage: ";
        msg += argv[0];
        msg.append(help_message_raw.data, help_message_raw.len);
        return msg;
    }

    /// This callback is called whenever an error occurs during parsing.
    ///
    /// \param errmsg An error message that describes what went wrong.
    /// \return `true` if the parsing process should continue, and `false` otherwise.
    static inline std::function<bool(std::string&&)> error_handler = [](std::string&& errmsg) -> bool {
        std::cerr << argv[0] << ": " << errmsg << "\n";
        std::cerr << help();
        std::exit(1);
    };

    static void handle_error(std::string&& msg) { has_error = !error_handler(std::move(msg)); }

    template <typename t>
    static constexpr auto type_name() -> static_string<100> {
        static_string<100> buffer;
        if constexpr (std::is_same_v<t, string>) buffer.append("string");
        else if constexpr (std::is_same_v<t, bool>) buffer.append("bool");
        else if constexpr (std::is_same_v<t, integer>) buffer.append("number");
        else if constexpr (std::is_same_v<t, double>) buffer.append("number");
        else if constexpr (requires { t::is_file_data; }) buffer.append("file");
        else if constexpr (std::is_same_v<t, callback>) buffer.append("function");
        else if constexpr (is_vector_v<t>) {
            buffer.append(type_name<typename t::value_type>().data, type_name<typename t::value_type>().len);
            buffer.append("s");
        } else RAISE_COMPILE_ERROR("Option type must be std::string, bool, integer, double, or void(*)(), or a vector thereof");
        return buffer;
    }

#define ERR                                        \
    do {                                           \
        std::string msg = "Could not map file \""; \
        msg += path;                               \
        msg += "\": ";                             \
        msg += ::strerror(errno);                  \
        handle_error(std::move(msg));              \
        return {};                                 \
    } while (0)

    template <typename file_data_type>
    static file_data_type map_file(std::string_view path) {
        int fd = ::open(path.data(), O_RDONLY);
        if (fd < 0) [[unlikely]]
            ERR;

        struct stat s {};
        if (::fstat(fd, &s)) [[unlikely]]
            ERR;
        auto sz = size_t(s.st_size);
        if (sz == 0) [[unlikely]]
            ERR;

        auto* mem = (char*) ::mmap(nullptr, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
        if (mem == MAP_FAILED) [[unlikely]]
            ERR;

        if (::close(fd)) [[unlikely]]
            ERR;

        /// Construct the file contents.
        typename file_data_type::contents_type ret;
        auto pointer = reinterpret_cast<typename file_data_type::element_pointer>(mem);
        if constexpr (requires { ret.assign(pointer, sz); }) ret.assign(pointer, sz);
        else if constexpr (requires { ret.assign(pointer, pointer + sz); }) ret.assign(pointer, pointer + sz);
        else RAISE_COMPILE_ERROR("file_data_type::contents_type must have an assign method that takes a pointer and a size_t (or a begin and end iterator) as arguments.");

        if (::munmap(mem, sz)) [[unlikely]]
            ERR;

        file_data_type dat;
        dat.path = path;
        dat.contents = std::move(ret);
        return dat;
    }

    template <typename type>
    static base_type_t<type> make_arg(const char* start, size_t len) {
        using base_type = base_type_t<type>;

        std::string s{start, len};
        if constexpr (std::is_same_v<base_type, std::string>) return s;
        else if constexpr (requires { base_type::is_file_data; }) return map_file<base_type>(s);
        else if constexpr (std::is_same_v<base_type, int64_t>) {
            char *pos{};
            errno = 0;
            auto i = std::strtoll(s.data(), &pos, 10);
            if (errno == ERANGE) handle_error(s + " does not appear to be a valid integer");
            return int64_t(i);
        } else if constexpr (std::is_same_v<base_type, double>) {
            char *pos{};
            errno = 0;
            auto i = std::strtod(s.data(), &pos);
            if (errno == ERANGE) handle_error(s + " does not appear to be a valid integer");
            return double(i);
        } else if constexpr (std::is_same_v<base_type, callback>) RAISE_COMPILE_ERROR("Cannot make function arg.");
        else RAISE_COMPILE_ERROR("Option argument must be std::string, integer, double, or void(*)()");
    }

    template <typename option, static_string opt_name>
    static bool handle_option(std::string opt_str) {
        /// This function only handles named options.
        if constexpr (is_positional_v<option>) return false;
        else {
            auto sv = opt_name.sv();
            /// If the supplied string doesn't start with the option name, move on to the next option
            if (!opt_str.starts_with(sv)) return false;

            static constexpr bool is_multiple = requires { option::is_multiple; };
            if constexpr (!is_multiple) {
                auto check_duplicate = [&] {
                    if (found<opt_name>()) {
                        std::string errmsg;
                        errmsg += "Duplicate option: \"";
                        errmsg += opt_str;
                        errmsg += "\"";
                        handle_error(std::move(errmsg));
                        return false;
                    }
                    return true;
                };
                /// Duplicate options are not allowed by default.
                if (!check_duplicate()) return false;
            }

            using base_type = base_type_t<typename option::type>;

            /// Flags don't have arguments.
            if constexpr (std::is_same_v<base_type, bool>) {
                if (opt_str != opt_name.sv()) return false;
                set_found<opt_name>();
                return true;
            }

            /// If this is a function argument, simply call the callback and we're done
            else if constexpr (std::is_same_v<base_type, callback>) {
                if (opt_str != opt_name.sv()) return false;
                set_found<opt_name>();
                if constexpr (requires { option::is_help_option; }) {
                    std::string h = help();
                    option::callback((void*) &h);
                }
                option::callback(option::argument);
                return true;
            }

            else {
                /// Both --option value and --option=value are
                /// valid ways of supplying a value. Test for
                /// both of them.

                /// --option=value
                if (opt_str.size() > opt_name.size()) {
                    if (opt_str[opt_name.size()] != '=') return false;
                    auto opt_start_offs = opt_name.size() + 1;
                    if constexpr (is_multiple) {
                        ref<opt_name>().push_back(make_arg<typename option::type>(opt_str.data() + opt_start_offs, opt_str.size() - opt_start_offs));
                    } else {
                        ref<opt_name>() = make_arg<typename option::type>(opt_str.data() + opt_start_offs, opt_str.size() - opt_start_offs);
                    }
                    set_found<opt_name>();
                    return true;
                }

                /// --option value
                if (++argi == argc) {
                    handle_error(std::string{"Missing argument for option \""} + opt_str + "\"");
                    return false;
                }
                if constexpr (is_multiple) {
                    ref<opt_name>().push_back(make_arg<typename option::type>(argv[argi], __builtin_strlen(argv[argi])));
                } else {
                    ref<opt_name>() = make_arg<typename option::type>(argv[argi], __builtin_strlen(argv[argi]));
                }
                set_found<opt_name>();
                return true;
            }
        }
    }

    template <typename opt>
    static bool handle_positional(std::string opt_str) {
        /// This function only cares about positional options.
        if constexpr (not is_positional_v<opt>) return false;
        else {
            /// If we've already encountered this positional option, then return.
            static constexpr bool is_multiple = requires { opt::is_multiple; };
            if constexpr (!is_multiple) {
                if (found<opt::name>()) return false;
            }

            /// Otherwise, attempt to parse this as the option value.
            set_found<opt::name>();
            if constexpr (is_multiple) ref<opt::name>().push_back(make_arg<typename opt::type>(opt_str.data(), opt_str.size()));
            else ref<opt::name>() = make_arg<typename opt::type>(opt_str.data(), opt_str.size());
            return true;
        }
    }

    template <typename type, static_string opt_name, bool required>
    static bool validate_option() {
        if (!found<opt_name>() && required) {
            std::string errmsg;
            errmsg += "Option \"";
            errmsg += opt_name.sv();
            errmsg += "\" is required";
            handle_error(std::move(errmsg));
            return false;
        }
        return true;
    }

public:
    static void parse(int _argc, char** _argv) {
        has_error = false;
        argc = _argc;
        argv = _argv;

        if (opts_parsed) {
            handle_error("Cannot parse options twice");
            return;
        } else {
            opts_parsed = true;
        }

        for (argi = 1; argi < argc; argi++) {
            if (has_error) return;
            const std::string opt_str{argv[argi], __builtin_strlen(argv[argi])};

            if (!(handle_option<opts, opts::name>(opt_str) || ...)) {
                if (!(handle_positional<opts>(opt_str) || ...)) {
                    std::string errmsg;
                    errmsg += "Unrecognized option: \"";
                    errmsg += opt_str;
                    errmsg += "\"";
                    handle_error(std::move(errmsg));
                }
            }
        }

        (validate_option<typename opts::type, opts::name, opts::is_required>() && ...);
    }
};

} // namespace command_line_options

#undef RAISE_COMPILE_ERROR
#undef ERR
#endif // CLOPTS_H
