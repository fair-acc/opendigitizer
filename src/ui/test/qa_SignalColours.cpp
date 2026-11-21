#include "components/SignalColours.hpp"
#include <boost/ut.hpp>

#include <ranges>
#include <set>

int main() {
    using namespace boost::ut;
    using namespace gr::od;
    using Style = SignalColours::Style;

    "slots are handed out in palette order and reused after release"_test = [] {
        SignalColours colours;
        const auto&   palette = colours.palette();

        const std::size_t s1 = colours.acquire();
        const std::size_t s2 = colours.acquire();
        expect(eq(s1, 0UZ));
        expect(eq(s2, 1UZ));
        expect(eq(colours.colour(s1), palette[0]));
        expect(eq(colours.colour(s2), palette[1]));

        colours.release(s1);
        expect(eq(colours.acquire(), s1)) << "a released slot is handed out first";
        expect(eq(colours.acquire(), 2UZ));
    };

    "each style has its own palette"_test = [] {
        SignalColours colours;
        colours.style                   = Style::Dark;
        const std::size_t   s1          = colours.acquire();
        const std::size_t   s2          = colours.acquire();
        const std::uint32_t darkColour1 = colours.colour(s1);
        expect(neq(s1, s2));
        colours.release(s1);
        expect(eq(colours.acquire(), s1));

        colours.style = Style::Light;
        expect(neq(colours.colour(s1), darkColour1));
    };

    "an explicit colour claims its palette slot, or appends one"_test = [] {
        SignalColours       colours;
        const std::uint32_t second = colours.palette()[1];

        expect(eq(colours.acquire(second), 1UZ)) << "a free palette colour keeps its slot";
        expect(eq(colours.acquire(), 0UZ));
        expect(eq(colours.acquire(), 2UZ)) << "the claimed slot is skipped";

        const std::size_t paletteSize = colours.palette().size();
        const std::size_t appended    = colours.acquire(second);
        expect(eq(appended, paletteSize)) << "a colour already in use appends a slot";
        expect(eq(colours.colour(appended), second));

        const std::size_t offPalette = colours.acquire(0xFF00FFU + 1U);
        expect(eq(offPalette, paletteSize + 1UZ));
        expect(eq(colours.colour(offPalette), 0xFF00FFU + 1U));
    };

    "overflow colours are deterministic, distinct and never 0"_test = [] {
        SignalColours first;
        SignalColours second;
        first.style  = Style::Dark;
        second.style = Style::Dark;

        const std::size_t       paletteSize = first.palette().size();
        std::set<std::uint32_t> seen(first.palette().begin(), first.palette().end());
        for (std::size_t i = 0UZ; i < paletteSize + 10UZ; ++i) {
            const std::size_t slot = first.acquire();
            expect(eq(slot, i));
            expect(eq(second.acquire(), slot));
            expect(eq(first.colour(slot), second.colour(slot))) << "same colour in every instance";
            if (slot >= paletteSize) {
                expect(neq(first.colour(slot), 0U)) << "0 means 'unset' for sinks";
                expect(seen.insert(first.colour(slot)).second) << "overflow colour differs from all others";
            }
        }
    };

    "ManagedColour holds a slot of the shared instance"_test = [] {
        {
            const ManagedColour sc1;
            const ManagedColour sc2;
            expect(neq(sc1.colour(), 0U));
            expect(neq(sc2.colour(), 0U));
            expect(neq(sc1.colour(), sc2.colour()));
            expect(neq(sc1._localSlot, sc2._localSlot));
        }
        const ManagedColour sc3;
        expect(eq(sc3._localSlot, 0UZ)) << "the destructors released their slots";
    };

    "ManagedColour follows the style of the shared instance"_test = [] {
        auto&               shared = SignalColours::instance();
        const ManagedColour sc;
        const std::uint32_t lightColour = sc.colour();
        shared.style                    = Style::Dark;
        const std::uint32_t darkColour  = sc.colour();
        shared.style                    = Style::Light;
        expect(neq(lightColour, 0U));
        expect(neq(darkColour, 0U));
        expect(neq(darkColour, lightColour));
    };

    "ManagedColour::setColour"_test = [] {
        ManagedColour sc;
        sc.setColour(0xFF00FF);
        expect(eq(sc.colour(), 0xFF00FFU));
    };

    return 0;
}
