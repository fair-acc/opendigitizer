#include "../utils/BrowserHistory.hpp"

#include <boost/ut.hpp>

using Digitizer::AppLocation;
using DigitizerUi::BrowserHistory;

const boost::ut::suite<"BrowserHistory"> tests = [] {
    using namespace boost::ut;

    const AppLocation demo  = AppLocation::forDashboard("Demo");
    const AppLocation other = AppLocation::forDashboard("Other");

    "starts at the root without a pending change"_test = [] {
        BrowserHistory history;
        expect(history.current() == AppLocation{});
        expect(!history.takeBrowserChange().has_value());
    };

    "push and replace update the current location"_test = [&] {
        BrowserHistory history;
        history.push(demo);
        expect(history.current() == demo);
        history.replace(other);
        expect(history.current() == other);
        expect(!history.takeBrowserChange().has_value()) << "own writes are not browser changes";
    };

    "a browser change is delivered once"_test = [&] {
        BrowserHistory history;
        history.onBrowserLocation(demo);
        expect(history.current() == demo);
        expect(history.takeBrowserChange() == std::optional<AppLocation>{demo});
        expect(!history.takeBrowserChange().has_value());
    };

    "hashchange and popstate for the same navigation deliver one change"_test = [&] {
        BrowserHistory history;
        history.onBrowserLocation(demo);
        history.onBrowserLocation(demo);
        expect(history.takeBrowserChange().has_value());
        expect(!history.takeBrowserChange().has_value());
    };

    "pushing the location the browser just reported is a no-op"_test = [&] {
        BrowserHistory history;
        history.onBrowserLocation(demo);
        std::ignore = history.takeBrowserChange();
        history.push(demo);
        expect(!history.takeBrowserChange().has_value());
        expect(history.current() == demo);
    };

    "rapid browser changes keep the latest"_test = [&] {
        BrowserHistory history;
        history.onBrowserLocation(demo);
        history.onBrowserLocation(other);
        expect(history.takeBrowserChange() == std::optional<AppLocation>{other});
        expect(!history.takeBrowserChange().has_value());
    };

    "a local push supersedes an unprocessed browser change"_test = [&] {
        BrowserHistory history;
        history.onBrowserLocation(demo);
        history.push(other);
        expect(!history.takeBrowserChange().has_value());
        expect(history.current() == other);
    };

    "going back to an earlier location is reported even if it was pushed before"_test = [&] {
        BrowserHistory history;
        history.push(demo);
        history.push(other);
        history.onBrowserLocation(demo); // back
        expect(history.takeBrowserChange() == std::optional<AppLocation>{demo});
    };

    "link without a page is a relative fragment"_test = [&] {
        BrowserHistory history;
        expect(history.linkTo(AppLocation::forDashboard("My Board")) == "#/dashboard/My%20Board");
    };
};

int main() { return boost::ut::cfg<boost::ut::override>.run(); }
