#ifndef CLOPTS_H
#define CLOPTS_H
#include <functional>
#include <iostream>
#include <map>
#include <variant>

#define CONSTEXPR_NOT_IMPLEMENTED(msg)            \
	[]<bool x = false> { static_assert(x, msg); } \
	()

namespace command_line_options {

template <size_t sz>
struct static_string {
	char   data[sz]{};
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

template <static_string _name, static_string _description = "", typename _type = std::string, bool required = false>
struct option {
	static_assert(sizeof _description.data < 512, "Description may not be longer than 512 characters");
	static_assert(_name.len > 0, "Option name may not be empty");
	static_assert(sizeof _name.data < 256, "Option name may not be longer than 256 characters");
	static_assert(!std::is_void_v<_type>, "Option type may not be void. Use bool instead");
	static_assert(std::is_same_v<_type, std::string>	//
					  || std::is_same_v<_type, bool>	//
					  || std::is_same_v<_type, double>	//
					  || std::is_same_v<_type, int64_t> //
					  || std::is_same_v<_type, void (*)(void*)>,
		"Option type must be std::string, bool, int64_t, double, or void(*)()");

	using type												   = _type;
	static constexpr inline decltype(_name)		   name		   = _name;
	static constexpr inline decltype(_description) description = _description;
	static constexpr inline bool				   is_flag	   = std::is_same_v<_type, bool>;
	static constexpr inline bool				   is_required = required;

	constexpr option() = delete;
};

template <static_string _name, static_string _description, typename _type = std::string, bool required = true>
struct positional : option<_name, _description, _type, required> {
	static constexpr inline bool is_positional = true;
};

template <static_string _name, static_string _description, void (*f)(void*), bool required = false, void* arg = nullptr>
struct func : public option<_name, _description, void (*)(void*), required> {
	static constexpr inline decltype(f) callback = f;
	static inline void*					argument = arg;

	constexpr func() = delete;
};

template <static_string _name, static_string _description = "", bool required = false>
struct flag : public option<_name, _description, bool, required> {
	constexpr flag() = delete;
};

struct help : public func<"--help", "Print this help information", print_help_and_exit> {
	constexpr help()							= delete;
	static constexpr inline bool is_help_option = true;
};

template <typename... opts>
struct clopts {
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
	static_assert(check_duplicate_options(), "Two different options may not have the same name");

	using help_string_t = static_string<1024 * sizeof...(opts)>;
	using string		= std::string;
	using integer		= int64_t;
	using callback		= void (*)(void*);

	template <static_string s, typename... options>
	struct type_of;

	template <static_string s, typename option, typename option2>
	struct type_of<s, option, option2> {
		using type = std::conditional_t<s == option::name, typename option::type, typename option2::type>;
	};

	template <static_string s, typename option, typename... options>
	struct type_of<s, option, options...> {
		using type = std::conditional_t<sizeof...(options) == 0 || s == option::name, typename option::type,
			typename type_of<s, options...>::type>;
	};

	template <static_string s>
	using type_of_t = typename type_of<s, opts...>::type;

	struct parsed_options {
		struct option_data {
			std::variant<std::string, int64_t, double> value;
			bool									   found = false;
		};
		std::map<std::string, option_data> options;

		parsed_options() { ((options[std::string{opts::name.sv()}] = {}), ...); }

		template <typename tstring, tstring key>
		static constexpr void assert_has_key() {
			static_assert(((opts::name.len == key.size() && __builtin_strcmp(opts::name.data, key.c_str()) == 0) || ...),
				"Invalid option name. You've probably misspelt an option.");
		}

		template <typename tstring, tstring key>
		[[nodiscard]] static auto as_std_string() -> std::string {
			if constexpr (std::is_same_v<std::remove_cvref_t<tstring>, std::string>) return key;
			else return std::string{key.c_str(), key.size()};
		}

		template <typename tstring, tstring key>
		[[nodiscard]] auto get_value() -> option_data& {
			static const std::string k = as_std_string<tstring, key>();
			assert_has_key<tstring, key>();
			return options[k];
		}

		template <static_string s>
		[[nodiscard]] auto get() const -> type_of_t<s> {
			using value_type		   = type_of_t<s>;
			static const std::string k = as_std_string<decltype(s), s>();
			assert_has_key<decltype(s), s>();
			if constexpr (std::is_same_v<value_type, bool>) return options.at(k).found;
			if constexpr (std::is_same_v<value_type, callback>) CONSTEXPR_NOT_IMPLEMENTED("Cannot call get<>() on an option with function type.");
			else return std::get<value_type>(options.at(k).value);
		}

		template <typename tstring, tstring key>
		[[nodiscard]] bool has_value() const {
			static const std::string k = as_std_string<tstring, key>();
			assert_has_key<tstring, key>();
			return options.at(k).found;
		}

		template <static_string s>
		[[nodiscard]] auto has() const -> bool {
			static const std::string k = as_std_string<decltype(s), s>();
			assert_has_key<decltype(s), s>();
			return options.at(k).found;
		}

		template <typename type, static_string opt_name>
		void dump_option(std::ostream& s) const {
			static const std::string k = as_std_string<decltype(opt_name), opt_name>();
			assert_has_key<decltype(opt_name), opt_name>();
			if constexpr (std::is_same_v<type, bool>) s << k << ":" << std::boolalpha << options.at(k).found << "\n";
			if constexpr (std::is_same_v<type, callback>) s << k << "\n";
			else s << k << ":" << std::get<type>(options.at(k).value) << "\n";
		}

		std::ostream& dump(std::ostream& s) const {
			(dump_option<typename opts::type, opts::name>(s), ...);
			return s;
		}
	};

	static inline int	 argc;
	static inline int	 argi;
	static inline char** argv;

	constexpr clopts() = delete;

	static constexpr auto make_help_message() -> help_string_t {
		help_string_t msg{};

		/// Determine the length of the longest name + typename.
		size_t max_len{};
		auto   determine_max_len = [&]<typename opt> {
			  if constexpr (requires { opt::is_positional; }) return;
			  size_t combined_len = opt::name.len + (type_name<typename opt::type>().len + 3) * !opt::is_flag;
			  if (combined_len > max_len) max_len = combined_len;
		};
		(determine_max_len.template operator()<opts>(), ...);

		/// Append the positional options.
		msg.append(" ");
		auto append_positional_options = [&]<typename opt> {
			if constexpr (!requires { opt::is_positional; }) return;
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
			if constexpr (requires { opt::is_positional; }) return;

			/// Compute the padding for this option.
			const auto	 tname	 = type_name<typename opt::type>();
			const size_t len	 = opt::name.len + (3 + tname.len) * !opt::is_flag;
			const size_t padding = max_len - len;
			/// Append the name and the type arg if this is not a bool option.
			msg.append("    ");
			msg.append(opt::name.data, opt::name.len);
			if (!opt::is_flag) {
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

	static inline std::function<bool(std::string&&)> handle_error = [](std::string&& errmsg) -> bool {
		std::cerr << argv[0] << ": " << errmsg << "\n";
		std::cerr << help();
		std::exit(1);
	};

	template <typename t>
	static constexpr auto type_name() -> static_string<100> {
		static_string<100> buffer;
		if constexpr (std::is_same_v<t, string>) buffer.append("string");
		else if constexpr (std::is_same_v<t, bool>) buffer.append("bool");
		else if constexpr (std::is_same_v<t, integer>) buffer.append("number");
		else if constexpr (std::is_same_v<t, double>) buffer.append("number");
		else if constexpr (std::is_same_v<t, callback>) buffer.append("function");
		else CONSTEXPR_NOT_IMPLEMENTED("Option type must be std::string, bool, integer, double, or void(*)()");
		return buffer;
	}

	template <static_string s>
	[[nodiscard]] static auto get(parsed_options& o) -> typename parsed_options::option_data& {
		return o.template get_value<decltype(s), s>();
	}

	template <auto key>
	[[nodiscard]] static bool has(parsed_options& o) {
		return o.template has_value<decltype(key), key>();
	}

	template <typename type>
	static type make_arg(const char* start, size_t len) {
		std::string s{start, len};
		if constexpr (std::is_same_v<type, std::string>) return s;
		else if constexpr (std::is_same_v<type, int64_t>) {
			try {
				return std::stol(s);
			} catch (...) {
				handle_error(s + " does not appear to be a valid integer");
				return {};
			}
		} else if constexpr (std::is_same_v<type, double>) {
			try {
				return std::stod(s);
			} catch (...) {
				handle_error(s + " does not appear to be a valid floating-point number");
				return {};
			}

		} else if constexpr (std::is_same_v<type, callback>) CONSTEXPR_NOT_IMPLEMENTED("Cannot make function arg.");
		else CONSTEXPR_NOT_IMPLEMENTED("Option argument must be std::string, integer, double, or void(*)()");
	}

	template <typename option, static_string opt_name>
	static bool handle_option(parsed_options& options, std::string opt_str) {
		/// This function only handles named options.
		if constexpr (requires { option::is_positional; }) return false;
		else {
			auto check_duplicate = [&] {
				if (has<opt_name>(options)) {
					std::string errmsg;
					errmsg += "Duplicate option: \"";
					errmsg += opt_str;
					errmsg += "\"";
					handle_error(std::move(errmsg));
					return false;
				}
				return true;
			};
			auto sv = opt_name.sv();
			/// If the supplied string doesn't start with the option name, move on to the next option
			if (!opt_str.starts_with(sv)) return false;

			/// Duplicate options are not allowed by default.
			if (!check_duplicate()) return false;

			/// Flags don't have arguments.
			if constexpr (std::is_same_v<typename option::type, bool>) {
				if (opt_str != opt_name.sv()) return false;
				get<opt_name>(options).found = true;
				return true;
			}

			/// If this is a function argument, simply call the callback and we're done
			else if constexpr (std::is_same_v<typename option::type, callback>) {
				if (opt_str != opt_name.sv()) return false;
				get<opt_name>(options).found = true;
				if constexpr (requires { option::is_help_option; }) {
					std::string h = help();
					option::callback((void*) &h);
				} else option::callback(option::argument);
				return true;
			}

			else {
				/// Both --option value and --option=value are
				/// valid ways of supplying a value. Test for
				/// both of them.

				/// --option=value
				if (opt_str.size() > opt_name.size()) {
					if (opt_str[opt_name.size()] != '=') return false;
					auto opt_start_offs			 = opt_name.size() + 1;
					get<opt_name>(options).found = true;
					get<opt_name>(options).value = make_arg<typename option::type>(opt_str.data() + opt_start_offs, opt_str.size() - opt_start_offs);
					return true;
				}

				/// --option value
				if (++argi == argc) {
					handle_error(std::string{"Missing argument for option \""} + opt_str + "\"");
					return false;
				}
				get<opt_name>(options).found = true;
				get<opt_name>(options).value = make_arg<typename option::type>(argv[argi], __builtin_strlen(argv[argi]));
				return true;
			}
		}
	}

	template <typename opt>
	static bool handle_positional(parsed_options& options, std::string opt_str) {
		/// This function only cares about positional options.
		if constexpr (!requires { opt::is_positional; }) return false;
		else {
			/// If we've already encountered this positional option, then return.
			if (has<opt::name>(options)) return false;

			/// Otherwise, attempt to parse this as the option value.
			get<opt::name>(options).found = true;
			get<opt::name>(options).value = make_arg<typename opt::type>(opt_str.data(), opt_str.size());
			return true;
		}
	}

	template <typename type, static_string opt_name, bool required>
	static bool validate_option(parsed_options& options) {
		if (!get<opt_name>(options).found && required) {
			std::string errmsg;
			errmsg += "Option \"";
			errmsg += opt_name.sv();
			errmsg += "\" is required";
			handle_error(std::move(errmsg));
			return false;
		}
		return true;
	}

	static auto parse(int _argc, char** _argv) -> parsed_options {
		parsed_options options;
		argc = _argc;
		argv = _argv;

		for (argi = 1; argi < argc; argi++) {
			const std::string opt_str{argv[argi], __builtin_strlen(argv[argi])};

			if (!(handle_option<opts, opts::name>(options, opt_str) || ...)) {
				if (!(handle_positional<opts>(options, opt_str) || ...)) {
					std::string errmsg;
					errmsg += "Unrecognized option: \"";
					errmsg += opt_str;
					errmsg += "\"";
					if (!handle_error(std::move(errmsg))) break;
				}
			}
		}

		if (!(validate_option<typename opts::type, opts::name, opts::is_required>(options) || ...)) return {};
		return options;
	}
};

} // namespace command_line_options

#undef CONSTEXPR_NOT_IMPLEMENTED

#endif // CLOPTS_H