#include "../include/AppLocation.hpp"

#include <boost/ut.hpp>

#include <string>
#include <vector>

using Digitizer::AppLocation;

const boost::ut::suite<"AppLocation"> tests = [] {
    using namespace boost::ut;
    using Segments = std::vector<std::string>;

    "parse"_test = [] {
        "dashboard route"_test = [] {
            const AppLocation location = AppLocation::parse("/dashboard/Demo");
            expect(location.segments == Segments{"dashboard", "Demo"});
            expect(location.query.empty());
            expect(location.dashboardName() == std::optional<std::string>{"Demo"});
        };

        "leading hash and query flags"_test = [] {
            const AppLocation location = AppLocation::parse("#/dashboard/Demo?darkMode&editable=true");
            expect(location.dashboardName() == std::optional<std::string>{"Demo"});
            expect(location.query.size() == 2UZ);
            expect(location.query.at("darkMode").empty());
            expect(location.query.at("editable") == "true");
        };

        "empty input is the root"_test = [] {
            expect(AppLocation::parse("").segments.empty());
            expect(AppLocation::parse("#").segments.empty());
            expect(AppLocation::parse("/").segments.empty());
            expect(!AppLocation::parse("").dashboardName().has_value());
        };

        "missing leading slash and repeated slashes"_test = [] {
            expect(AppLocation::parse("dashboard/Demo").segments == Segments{"dashboard", "Demo"});
            expect(AppLocation::parse("//dashboard///Demo/").segments == Segments{"dashboard", "Demo"});
        };

        "percent decoding"_test = [] {
            expect(AppLocation::parse("/dashboard/My%20Board").dashboardName() == std::optional<std::string>{"My Board"});
            expect(AppLocation::parse("/dashboard/a%2Fb%3Fc%23d").segments == Segments{"dashboard", "a/b?c#d"});
            expect(AppLocation::parse("/dashboard/%C3%A4").dashboardName() == std::optional<std::string>{"\xC3\xA4"}); // U+00E4
            expect(AppLocation::parse("/?k=a%26b%3Dc").query.at("k") == "a&b=c");
        };

        "malformed percent escapes stay literal"_test = [] {
            expect(AppLocation::parse("/dashboard/100%").dashboardName() == std::optional<std::string>{"100%"});
            expect(AppLocation::parse("/dashboard/%zz").dashboardName() == std::optional<std::string>{"%zz"});
            expect(AppLocation::parse("/dashboard/%2").dashboardName() == std::optional<std::string>{"%2"});
        };

        "empty value and bare key are equal"_test = [] {
            const bool equal = AppLocation::parse("/?a=&b").query == AppLocation::parse("/?a&b=").query;
            expect(equal);
        };

        "later duplicate key wins"_test = [] { expect(AppLocation::parse("/?k=1&k=2").query.at("k") == "2"); };

        "only the dashboard route has a name"_test = [] {
            expect(!AppLocation::parse("/dashboard").dashboardName().has_value());
            expect(!AppLocation::parse("/dashboard/a/b").dashboardName().has_value());
            expect(!AppLocation::parse("/other/Demo").dashboardName().has_value());
        };
    };

    "toHash"_test = [] {
        "canonical dashboard link"_test = [] {
            expect(AppLocation::forDashboard("Demo").toHash() == "/dashboard/Demo");
            expect(AppLocation::forDashboard("Demo", {{"darkMode", ""}}).toHash() == "/dashboard/Demo?darkMode");
            expect(AppLocation::forDashboard("Demo", {{"darkMode", ""}, {"editable", ""}}).toHash() == "/dashboard/Demo?darkMode&editable");
        };

        "root"_test = [] { expect(AppLocation{}.toHash() == "/"); };

        "reserved characters are percent encoded"_test = [] {
            expect(AppLocation::forDashboard("My Board").toHash() == "/dashboard/My%20Board");
            expect(AppLocation::forDashboard("a/b?c#d&e").toHash() == "/dashboard/a%2Fb%3Fc%23d%26e");
            expect(AppLocation::forDashboard("\xC3\xA4").toHash() == "/dashboard/%C3%A4");
        };

        "query values are encoded, bare keys have no equals sign"_test = [] {
            AppLocation location;
            location.query = {{"flag", ""}, {"k", "a&b=c"}};
            expect(location.toHash() == "/?flag&k=a%26b%3Dc");
        };
    };

    "round trip"_test = [] {
        for (const std::string name : {"Demo", "My Board", "a/b", "a?b", "a#b", "a&b=c", "100%", "%41", "\xC3\xA4\xE2\x82\xAC", "~._-", "+", "v1.2", "a:b"}) {
            const AppLocation location = AppLocation::forDashboard(name, {{"darkMode", ""}, {"k", name}});
            const AppLocation parsed   = AppLocation::parse('#' + location.toHash());
            expect(parsed == location) << name;
            expect(parsed.segments.at(1) == name) << name;
        }
    };

    "dashboard names from the URL are validated"_test = [] {
        "names that select a remote or parent path are rejected"_test = [] {
            for (const std::string hash : {"/dashboard/http%3A%2F%2Fevil.example%2Fd.grc", "/dashboard/https%3A%2F%2Fevil.example", "/dashboard/..%2F..%2Fsecret", "/dashboard/..", "/dashboard/.", "/dashboard/a%5Cb", "/dashboard/a%3Fb", "/dashboard/a%23b", "/dashboard/a%00b", "/dashboard/a%0Ab", "/dashboard/%7F", "/dashboard/%2Fetc%2Fpasswd"}) {
                expect(!AppLocation::parse(hash).dashboardName().has_value()) << hash;
            }
        };

        "ordinary names are accepted"_test = [] {
            for (const std::string name : {"Demo", "My Board", "v1.2", "a:b", "100%", "\xC3\xA4", "a&b=c", "...", "-"}) {
                expect(AppLocation::forDashboard(name).dashboardName() == std::optional<std::string>{name}) << name;
            }
        };

        "isValidDashboardName"_test = [] {
            expect(AppLocation::isValidDashboardName("Demo"));
            expect(!AppLocation::isValidDashboardName(""));
            expect(!AppLocation::isValidDashboardName(std::string("a\0b", 3)));
            expect(!AppLocation::isValidDashboardName("a/b"));
        };
    };

    "flag"_test = [] {
        const AppLocation location = AppLocation::parse("/?a&b=true&c=1&d=false&e=0&f=maybe");
        expect(location.flag("a") == std::optional<bool>{true});
        expect(location.flag("b") == std::optional<bool>{true});
        expect(location.flag("c") == std::optional<bool>{true});
        expect(location.flag("d") == std::optional<bool>{false});
        expect(location.flag("e") == std::optional<bool>{false});
        expect(!location.flag("f").has_value());
        expect(!location.flag("missing").has_value());
    };
};

int main() { return boost::ut::cfg<boost::ut::override>.run(); }
