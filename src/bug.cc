#include <iostream>
#include <variant>

/// `a` and `b` must have the same length.
constexpr bool static_streq(const char* a, const char* b) {
	while (*a) {
		if (*a != *b) return false;
		a++;
		b++;
	}
	return true;
};

template <size_t sz>
struct static_string {
	char   data[sz]{};
	size_t len{};

	constexpr static_string(const char (&_data)[sz]) {
		std::copy_n(_data, sz, data);
		len = sz - 1;
	}

	template<typename str>
	constexpr bool operator==(const str& s) const { return len == s.len && static_streq(data, s.data); }

	constexpr auto c_str() const -> const char* { return data; }
	constexpr auto size() const -> size_t { return len; }
};

template <static_string _name, typename _type = std::string>
struct option {
	using type												   = _type;
	static constexpr inline decltype(_name)		   name		   = _name;
};

template <typename... opts>
struct clopts {
	template<static_string s, typename ...options>
	struct type_of;

	template<static_string s, typename option, typename option2>
	struct type_of<s, option, option2> {
		using type = std::conditional_t<s == option::name, typename option::type, typename option2::type>;
	};

	template<static_string s, typename option, typename ...options>
	struct type_of<s, option, options...> {
		using type = std::conditional_t<sizeof...(options) == 0 || s == option::name, typename option::type,
			typename type_of<s, options...>::type>;
	};

	template<static_string s>
	using type_of_t = typename type_of<s, opts...>::type;

	struct parsed_options {
		template<static_string s>
		[[nodiscard]] type_of_t<s> get() { return {}; }
	};
};

using options = clopts< // clang-format off
	option<"--filename">,
	option<"--frobnicate", bool>,
	option<"--size", int64_t>
>; // clang-format on

int main() {
	options::parsed_options opts;
	opts.get<"--filename">();
}