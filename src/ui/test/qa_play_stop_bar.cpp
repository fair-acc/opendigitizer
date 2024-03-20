#include <boost/ut.hpp>

#include <atomic>
#include <fmt/format.h>
#include <map>
#include <vector>

#include "../toolbar_block.hpp"

#ifdef __GNUC__
#pragma GCC diagnostic push // ignore warning of external libraries that from this lib-context we do not have any control over
#ifndef __clang__
#pragma GCC diagnostic ignored "-Wuseless-cast"
#endif
#pragma GCC diagnostic ignored "-Wsign-conversion"
#endif
#include <magic_enum.hpp>
#include <magic_enum_utility.hpp>
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

const static boost::ut::suite state_machine = [] {
    using namespace std::string_literals;
    using namespace boost::ut;
    using namespace DigitizerUi::play_stop;

    "StateMachine all State transitions"_test = [] {
        using enum DigitizerUi::play_stop::State;

        std::map<State, std::vector<State>> allowedTransitions = {
            { PlayStop, { Pause, Stopped } },
            { Play, { Pause, Stopped } },
            { PlayStream, { Pause, Stopped } },
            { Pause, { PlayStop, Play, PlayStream, Stopped } },
            { Stopped, { PlayStop, Play, PlayStream } },
            { Error, { Stopped } },
        };

        magic_enum::enum_for_each<State>([&allowedTransitions](State fromState) {
            magic_enum::enum_for_each<State>([&fromState, &allowedTransitions](State toState) {
                bool isAllowed = std::find(allowedTransitions[fromState].begin(), allowedTransitions[fromState].end(),
                                         toState)
                              != allowedTransitions[fromState].end();

                bool isValid = isValidTransition(fromState, toState);

                // Assert that the function's validity matches the expected validity
                expect(isValid == isAllowed)
                        << fmt::format("Transition from {} to {} should be {}", magic_enum::enum_name(fromState),
                                   magic_enum::enum_name(toState),
                                   isAllowed ? "allowed" : "disallowed");
            });
        });
    };

    // TODO: continue unit-tests for
    // * disabled state
    // * probably need to introduce different 'pause' states to return to the original either 'play' or 'streaming' state, ...
    // *
};

int main() { /* not needed for ut */
}
