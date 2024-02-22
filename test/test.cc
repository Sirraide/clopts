#include "../include/clopts.hh"

#include <catch2/catch_all.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <fstream>

using namespace command_line_options;
using namespace Catch::literals;
using namespace std::literals;

static bool error_handler(std::string&& s) {
    throw std::runtime_error(s);
}

using basic_options = clopts<
    option<"--string", "A string", std::string>,
    option<"--number", "A number", int64_t>,
    option<"--float", "A float", double>>;

TEST_CASE("Options are nullptr by default") {
    auto opts = basic_options::parse(0, nullptr, error_handler);
    CHECK(not opts.get<"--string">());
    CHECK(not opts.get<"--number">());
    CHECK(not opts.get<"--float">());
}

TEST_CASE("Options can be parsed") {
    std::array args = {
        "test",
        "--string",
        "Hello, world!",
        "--number",
        "42",
        "--float",
        "3.141592653589",
    };

    auto opts1 = basic_options::parse(args.size(), args.data(), error_handler);
    REQUIRE(opts1.get<"--string">());
    REQUIRE(opts1.get<"--number">());
    REQUIRE(opts1.get<"--float">());
    CHECK(*opts1.get<"--string">() == "Hello, world!");
    CHECK(*opts1.get<"--number">() == 42);
    CHECK(*opts1.get<"--float">() == 3.141592653589_a);

    SECTION("multiple times") {
        auto opts2 = basic_options::parse(args.size(), args.data(), error_handler);

        REQUIRE(opts2.get<"--string">());
        REQUIRE(opts2.get<"--number">());
        REQUIRE(opts2.get<"--float">());

        CHECK(*opts2.get<"--string">() == "Hello, world!");
        CHECK(*opts2.get<"--number">() == 42);
        CHECK(*opts2.get<"--float">() == 3.141592653589_a);
    }
}

TEST_CASE("Options can be parsed out of order") {
    std::array args = {
        "test",
        "--float",
        "3.141592653589",
        "--number",
        "42",
        "--string",
        "Hello, world!",
    };

    auto opts = basic_options::parse(args.size(), args.data(), error_handler);
    REQUIRE(opts.get<"--string">());
    REQUIRE(opts.get<"--number">());
    REQUIRE(opts.get<"--float">());
    CHECK(*opts.get<"--string">() == "Hello, world!");
    CHECK(*opts.get<"--number">() == 42);
    CHECK(*opts.get<"--float">() == 3.141592653589_a);
}

TEST_CASE("Required options must be present") {
    using options = clopts<option<"--required", "A required option", std::string, true>>;
    CHECK_THROWS(options::parse(0, nullptr, error_handler));
}

TEST_CASE("Flags are never required") {
    using options = clopts<flag<"--flag", "A flag">>;
    CHECK_NOTHROW(options::parse(0, nullptr, error_handler));
}

TEST_CASE("Setting a custom error handler works") {
    using options = clopts<option<"--required", "A required option", std::string, true>>;
    bool called = false;
    options::parse(0, nullptr, [&](std::string&&) { return called = true; });
    CHECK(called);
}

TEST_CASE("values<> option type is handled properly") {
    using int_options = clopts<option<"--values", "A values option", values<0, 1, 2, 3>>>;
    using string_options = clopts<option<"--values", "A values option", values<"foo", "bar", "baz">>>;

    SECTION("Correct values are accepted") {
        std::array int_args = {
            "test",
            "--values",
            "1",
        };
        std::array string_args = {
            "test",
            "--values",
            "foo",
        };

        auto int_opts = int_options::parse(int_args.size(), int_args.data(), error_handler);
        auto string_opts = string_options::parse(string_args.size(), string_args.data(), error_handler);

        REQUIRE(int_opts.get<"--values">());
        REQUIRE(string_opts.get<"--values">());
        CHECK(*int_opts.get<"--values">() == 1);
        CHECK(*string_opts.get<"--values">() == "foo");
    }

    SECTION("Invalid values are rejected") {
        std::array int_args = {
            "test",
            "--values",
            "4",
        };
        std::array string_args = {
            "test",
            "--values",
            "qux",
        };

        CHECK_THROWS(int_options::parse(int_args.size(), int_args.data(), error_handler));
        CHECK_THROWS(string_options::parse(string_args.size(), string_args.data(), error_handler));
    }
}

TEST_CASE("Positional options are handled correctly") {
    using options = clopts<
        positional<"first", "The first positional argument", std::string, false>,
        positional<"second", "The second positional argument", int64_t, false>,
        positional<"third", "The third positional argument", double, false>>;

    std::array args = {
        "test",
        "Hello, world!",
        "42",
        "3.141592653589",
    };

    auto opts = options::parse(args.size(), args.data(), error_handler);

    REQUIRE(opts.get<"first">());
    REQUIRE(opts.get<"second">());
    REQUIRE(opts.get<"third">());

    CHECK(*opts.get<"first">() == "Hello, world!");
    CHECK(*opts.get<"second">() == 42);
    CHECK(*opts.get<"third">() == 3.141592653589_a);
}

TEST_CASE("Positional and non-positional options mix properly") {
    using options = clopts<
        positional<"first", "The first positional argument", std::string, false>,
        positional<"second", "The second positional argument", int64_t, false>,
        positional<"third", "The third positional argument", double, false>,
        option<"--string", "A string", std::string>,
        option<"--number", "A number", int64_t>,
        option<"--float", "A float", double>>;

    std::array args = {
        "test",
        "--string",
        "Hello, world!",
        "foobarbaz",
        "24",
        "--number",
        "42",
        "6.283185307179",
        "--float",
        "3.141592653589",
    };

    auto opts = options::parse(args.size(), args.data(), error_handler);

    REQUIRE(opts.get<"first">());
    REQUIRE(opts.get<"second">());
    REQUIRE(opts.get<"third">());
    REQUIRE(opts.get<"--string">());
    REQUIRE(opts.get<"--number">());
    REQUIRE(opts.get<"--float">());

    CHECK(*opts.get<"first">() == "foobarbaz");
    CHECK(*opts.get<"second">() == 24);
    CHECK(*opts.get<"third">() == 6.283185307179_a);
    CHECK(*opts.get<"--string">() == "Hello, world!");
    CHECK(*opts.get<"--number">() == 42);
    CHECK(*opts.get<"--float">() == 3.141592653589_a);
}

TEST_CASE("Positional options are required by default") {
    using options = clopts<positional<"first", "The first positional argument">>;
    CHECK_THROWS(options::parse(0, nullptr, error_handler));
}

TEST_CASE("Positional values<> work") {
    using string_options = clopts<positional<"format", "Output format", values<"foo", "bar">>>;
    using int_options = clopts<positional<"format", "Output format", values<0, 1>>>;

    SECTION("Correct values are accepted") {
        std::array args1 = {"test", "foo"};
        std::array args2 = {"test", "bar"};
        std::array args3 = {"test", "0"};
        std::array args4 = {"test", "1"};

        auto opts1 = string_options::parse(args1.size(), args1.data(), error_handler);
        auto opts2 = string_options::parse(args2.size(), args2.data(), error_handler);
        auto opts3 = int_options::parse(args3.size(), args3.data(), error_handler);
        auto opts4 = int_options::parse(args4.size(), args4.data(), error_handler);

        REQUIRE(opts1.get<"format">());
        REQUIRE(opts2.get<"format">());
        REQUIRE(opts3.get<"format">());
        REQUIRE(opts4.get<"format">());

        CHECK(*opts1.get<"format">() == "foo");
        CHECK(*opts2.get<"format">() == "bar");
        CHECK(*opts3.get<"format">() == 0);
        CHECK(*opts4.get<"format">() == 1);
    }

    SECTION("Invalid values raise an error") {
        std::array args1 = {"test", "baz"};
        std::array args2 = {"test", "2"};

        CHECK_THROWS(string_options::parse(args1.size(), args1.data(), error_handler));
        CHECK_THROWS(int_options::parse(args2.size(), args2.data(), error_handler));
    }
}

TEST_CASE("Multiple positional values<> work") {
    using string_options = clopts<multiple<positional<"format", "Output format", values<"foo", "bar">>>>;
    using int_options = clopts<multiple<positional<"format", "Output format", values<0, 1>>>>;

    SECTION("Correct values are accepted") {
        std::array args1 = {"test", "foo", "bar", "foo"};
        std::array args2 = {"test", "0", "1", "1"};

        auto opts1 = string_options::parse(args1.size(), args1.data(), error_handler);
        auto opts2 = int_options::parse(args2.size(), args2.data(), error_handler);

        REQUIRE(opts1.get<"format">());
        REQUIRE(opts2.get<"format">());

        REQUIRE(opts1.get<"format">()->size() == 3);
        REQUIRE(opts2.get<"format">()->size() == 3);

        CHECK(opts1.get<"format">()->at(0) == "foo");
        CHECK(opts1.get<"format">()->at(1) == "bar");
        CHECK(opts1.get<"format">()->at(2) == "foo");
        CHECK(opts2.get<"format">()->at(0) == 0);
        CHECK(opts2.get<"format">()->at(1) == 1);
        CHECK(opts2.get<"format">()->at(2) == 1);
    }

    SECTION("Invalid values raise an error") {
        std::array args1 = {"test", "foo", "baz", "foo"};
        std::array args2 = {"test", "0", "2", "1"};

        CHECK_THROWS(string_options::parse(args1.size(), args1.data(), error_handler));
        CHECK_THROWS(int_options::parse(args2.size(), args2.data(), error_handler));
    }
}

TEST_CASE("Short option options are parsed properly") {
    using options = clopts<
        experimental::short_option<"s", "A string", std::string>,
        experimental::short_option<"n", "A number", int64_t>,
        experimental::short_option<"-f", "A float", double>>;

    std::array args = {
        "test",
        "sHello, world!",
        "n=42",
        "-f3.141592653589",
    };

    auto opts = options::parse(args.size(), args.data(), error_handler);

    REQUIRE(opts.get<"s">());
    REQUIRE(opts.get<"n">());
    REQUIRE(opts.get<"-f">());
    CHECK(*opts.get<"s">() == "Hello, world!");
    CHECK(*opts.get<"n">() == 42);
    CHECK(*opts.get<"-f">() == 3.141592653589_a);
}

TEST_CASE("Empty option value is handled correctly") {
    std::array args = {"test", "--empty="};

    SECTION("for strings") {
        using options = clopts<option<"--empty", "An empty string", std::string>>;
        auto opts = options::parse(args.size(), args.data(), error_handler);
        REQUIRE(opts.get<"--empty">());
        CHECK(opts.get<"--empty">()->empty());
    }

    SECTION("for integers") {
        using options = clopts<option<"--empty", "An empty integer", int64_t>>;
        CHECK_THROWS(options::parse(args.size(), args.data(), error_handler));
    }

    SECTION("for floats") {
        using options = clopts<option<"--empty", "An empty float", double>>;
        CHECK_THROWS(options::parse(args.size(), args.data(), error_handler));
    }

    SECTION("for values<> that contains the empty string") {
        using options = clopts<option<"--empty", "An empty value", values<"">>>;
        auto opts = options::parse(args.size(), args.data(), error_handler);
        REQUIRE(opts.get<"--empty">());
        CHECK(opts.get<"--empty">()->empty());
    }
}

TEST_CASE("Integer overflow is an error") {
    using options = clopts<option<"--overflow", "A number", int64_t>>;

    std::array args = {
        "test",
        "--overflow",
        "100000000000000000000000000000000000000000000000",
    };

    CHECK_THROWS(options::parse(args.size(), args.data(), error_handler));
}

TEST_CASE("Multiple meta-option") {
    using options = clopts<
        multiple<option<"--int", "Integers", int64_t, true>>,
        multiple<option<"--string", "Strings", std::string, true>>>;

    std::array args = {
        "test",
        "--int",
        "1",
        "--string",
        "foo",
        "--int",
        "2",
        "--string",
        "bar",
    };

    auto opts = options::parse(args.size(), args.data(), error_handler);

    REQUIRE(opts.get<"--int">());
    REQUIRE(opts.get<"--string">());

    CHECK(opts.get<"--int">()->size() == 2);
    CHECK(opts.get<"--string">()->size() == 2);

    CHECK(opts.get<"--int">()->at(0) == 1);
    CHECK(opts.get<"--int">()->at(1) == 2);
    CHECK(opts.get<"--string">()->at(0) == "foo");
    CHECK(opts.get<"--string">()->at(1) == "bar");
}

TEST_CASE("Multiple + Positional works") {
    using options = clopts<
        multiple<option<"--int", "Integers", int64_t, true>>,
        multiple<option<"--string", "Strings", std::string, true>>,
        multiple<positional<"rest", "The remaining arguments", std::string, false>>>;

    std::array args = {
        "test",
        "--int",
        "1",
        "baz",
        "--string",
        "foo",
        "--int",
        "2",
        "--string",
        "bar",
        "qux",
    };

    auto opts = options::parse(args.size(), args.data(), error_handler);

    REQUIRE(opts.get<"--int">());
    REQUIRE(opts.get<"--string">());
    REQUIRE(opts.get<"rest">());

    CHECK(opts.get<"--int">()->size() == 2);
    CHECK(opts.get<"--string">()->size() == 2);
    CHECK(opts.get<"rest">()->size() == 2);

    CHECK(opts.get<"--int">()->at(0) == 1);
    CHECK(opts.get<"--int">()->at(1) == 2);
    CHECK(opts.get<"--string">()->at(0) == "foo");
    CHECK(opts.get<"--string">()->at(1) == "bar");
    CHECK(opts.get<"rest">()->at(0) == "baz");
    CHECK(opts.get<"rest">()->at(1) == "qux");
}

TEST_CASE("Calling from main() works as expected") {
    using options = clopts<option<"--number", "A number", int64_t>>;

    std::array backing_args = {
        "test"s,
        "--number"s,
        "42"s,
    };

    std::array args = {
        backing_args[0].data(),
        backing_args[1].data(),
        backing_args[2].data(),
    };

    int argc = int(args.size());
    char** argv = args.data();
    auto opts = options::parse(argc, argv, error_handler);

    REQUIRE(opts.get<"--number">());
    CHECK(*opts.get<"--number">() == 42);
}

TEST_CASE("File option can map a file properly") {
    auto run = []<typename file> {
        using options = clopts<option<"file", "A file", file>>;

        std::array args = {
            "test",
            "file",
            __FILE__,
        };

        /// I can’t exactly test my file mapping code against itself.
        std::string_view _file_ = __FILE__;
        std::ifstream f{__FILE__};
        typename file::contents_type contents{std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
        typename file::path_type path{_file_.begin(), _file_.end()};

        auto opts = options::parse(args.size(), args.data(), error_handler);
        REQUIRE(opts.template get<"file">());
        CHECK(opts.template get<"file">()->path == path);
        CHECK(opts.template get<"file">()->contents == contents);
    };

    run.template operator()<file<>>();
    run.template operator()<file<std::vector<char>>>();
    run.template operator()<file<std::string, std::string>>();
    run.template operator()<file<std::string, std::vector<char>>>();
}

TEST_CASE("stop_parsing<> option") {
    using options = clopts<
        multiple<option<"--foo", "Foo option", std::string, true>>,
        flag<"--bar", "Bar option">,
        stop_parsing<"stop">>;

    SECTION("stops parsing") {
        std::array args = {
            "test",
            "--foo",
            "arg",
            "--foo",
            "stop", /// Argument of '--foo'
            "stop", /// Stop parsing
            "--bar",
            "--foo", /// Missing argument, but ignored because it’s after 'stop'.
        };

        auto opts = options::parse(args.size(), args.data(), error_handler);
        REQUIRE(opts.get<"--foo">());
        REQUIRE(opts.get<"--foo">()->size() == 2);
        CHECK(opts.get<"--foo">()->at(0) == "arg");
        CHECK(opts.get<"--foo">()->at(1) == "stop");
        CHECK(not opts.get<"--bar">());

        auto unprocessed = opts.unprocessed();
        REQUIRE(unprocessed.size() == 2);
        CHECK(unprocessed[0] == "--bar"sv);
        CHECK(unprocessed[1] == "--foo"sv);
    }

    SECTION("errors if there are missing required options") {
        std::array args = {
            "test",
            "stop"
        };

        CHECK_THROWS(options::parse(args.size(), args.data(), error_handler));
    }

    SECTION("is never required") {
        std::array args = {
            "test",
            "--foo",
            "arg",
        };

        auto opts = options::parse(args.size(), args.data(), error_handler);
        REQUIRE(opts.get<"--foo">());
        REQUIRE(opts.get<"--foo">()->size() == 1);
        CHECK(opts.get<"--foo">()->at(0) == "arg");
        CHECK(opts.unprocessed().empty());
    }

    SECTION("is effectively a no-op if it’s the last argument") {
        std::array args = {
            "test",
            "--foo",
            "arg",
            "stop",
        };

        auto opts = options::parse(args.size(), args.data(), error_handler);
        REQUIRE(opts.get<"--foo">());
        REQUIRE(opts.get<"--foo">()->size() == 1);
        CHECK(opts.get<"--foo">()->at(0) == "arg");
        CHECK(opts.unprocessed().empty());
    }

    SECTION("uses '--' by default") {
        using options2 = clopts<
            flag<"--bar", "Bar option">,
            stop_parsing<>>;

        std::array args = {
            "test",
            "--",
            "--bar",
        };

        auto opts = options2::parse(args.size(), args.data(), error_handler);
        REQUIRE(not opts.get<"--bar">());

        auto unprocessed = opts.unprocessed();
        REQUIRE(unprocessed.size() == 1);
        CHECK(unprocessed[0] == "--bar"sv);
    }
}

/*TEST_CASE("Aliased options are equivalent") {
    using options = clopts<
        multiple<option<"--string", "A string", std::string>>,
        aliases<"-s", "--string">
    >;

    std::array args = {
        "test",
        "--string",
        "123",
        "-s",
        "456",
    };

    auto opts = options::parse(args.size(), args.data(), error_handler);
    REQUIRE(opts.get<"--string">() == opts.get<"-s">());
    REQUIRE(opts.get<"-s">()->size() == 2);
    CHECK(opts.get<"-s">()->at(0) == "123");
    CHECK(opts.get<"-s">()->at(1) == "456");
}*/

/// TODO:
///  - alias<"-f", "--filename">; alternatively: option<names<"-f", "--filename">, "description">
///  - hidden<...> (don't show in help)
///  - Finish short_option