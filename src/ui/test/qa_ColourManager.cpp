#include "components/ColourManager.hpp"
#include <boost/ut.hpp>

int main() {
    using namespace boost::ut;
    using namespace opendigitizer;

    "ColourManager basic usage"_test = [] {
        auto& mgr = ColourManager::instance();
        mgr.reset();

        // acquire two *slot indices* from "misc"
        std::size_t s1 = mgr.getNextSlotIndex();
        std::size_t s2 = mgr.getNextSlotIndex();
        expect(s1 != s2) << "expect distinct slots for 'misc' palette";

        // release the first slot
        mgr.releaseSlotIndex(s1);

        // reacquire => expect to get the same slot index as s1
        const std::size_t s3 = mgr.getNextSlotIndex();
        expect(eq(s3, s1)) << "expect s3 == s1 after release";
    };

    "ColourManager palette specific usage"_test = [] {
        auto& mgr = ColourManager::instance();
        mgr.reset();

        // acquire two *slot indices* from "misc"
        std::size_t s1 = mgr.getNextSlotIndex("misc");
        std::size_t s2 = mgr.getNextSlotIndex("misc");
        expect(s1 != s2) << "expect distinct slots for 'misc' palette";

        // release the first slot
        mgr.releaseSlotIndex("misc", s1);

        // reacquire => expect to get the same slot index as s1
        const std::size_t s3 = mgr.getNextSlotIndex("misc");
        expect(eq(s3, s1)) << "expect s3 == s1 after release";
    };

    "ManagedColour usage"_test = [] {
        auto& mgr = ColourManager::instance();
        mgr.reset();

        {
            const ManagedColour sc1;
            auto                colour1 = sc1.colour();
            expect(colour1 != 0_u32) << "managed colour 1 should be non-zero";

            const ManagedColour sc2;
            auto                colour2 = sc2.colour();
            expect(colour2 != 0_u32) << "managed colour 2 should be non-zero";

            expect(neq(colour1, colour2)) << "managed colour 1 and 2 should not be identical";
            expect(neq(sc1._localSlot, sc2._localSlot)) << "managed colour slots should not be identical";
        }
        // no explicit release call; the destructor handled it
    };

    "Light/Dark mode usage"_test = [] {
        auto& mgr = ColourManager::instance();
        mgr.reset();

        mgr.setCurrentMode(ColourManager::ColourMode::Light);
        const ManagedColour sc; // from "matlab-light" by default
        std::uint32_t       lightColour = sc.colour();
        expect(lightColour != 0_u32) << "Should get a valid colour for light mode";

        mgr.setCurrentMode(ColourManager::ColourMode::Dark);
        std::uint32_t darkColour = sc.colour();
        expect(darkColour != 0_u32) << "Should get a valid colour for dark mode";

        expect(neq(darkColour, lightColour)) << "Should get a valid colour for dark mode";
    };

    "Overflow handling"_test = [] {
        auto& mgr = ColourManager::instance();
        mgr.reset();

        mgr.setOverflowStrategy(OverflowStrategy::ExtendAuto);

        // "adobe" has 7 base colours; request more than that
        // If auto-extends, it shouldn't throw
        bool threw = false;
        try {
            for (int i = 0; i < 10; ++i) {
                [[maybe_unused]] auto slot = mgr.getNextSlotIndex("adobe");
            }
        } catch (...) {
            threw = true;
        }
        expect(!threw) << "should not throw with ExtendAuto overflow strategy";
    };

    "Manual setColour usage"_test = [] {
        auto& mgr = ColourManager::instance();
        mgr.reset();

        ManagedColour sc;       // pick a slot
        sc.setColour(0xFF00FF); // forcibly set 0xFF00FF (magenta)
        expect(eq(sc.colour(), 0xFF00FF)) << "sc should hold 0xFF00FF now";
    };

    return 0;
}
