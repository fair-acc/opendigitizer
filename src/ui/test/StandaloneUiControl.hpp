#ifndef OPENDIGITIZER_TEST_STANDLONE_UI_CONTROL_HPP
#define OPENDIGITIZER_TEST_STANDLONE_UI_CONTROL_HPP

#include <gnuradio-4.0/Block.hpp>

#include <boost/ut.hpp>

namespace opendigitizer::test {

template<typename TControl>
struct StandaloneControl {
    TControl      block;
    gr::MsgPortIn sent;

    explicit StandaloneControl(gr::property_map settings) : block(std::move(settings)) {
        block.init(std::make_shared<gr::Sequence>());
        boost::ut::expect(block.msgOut.connect(sent).has_value()) << boost::ut::fatal;
    }

    [[nodiscard]] std::vector<gr::Message> takeSent() {
        auto&                    reader = sent.streamReader();
        auto                     span   = reader.get(reader.available());
        std::vector<gr::Message> messages(span.begin(), span.end());
        std::ignore = span.consume(span.size());
        return messages;
    }
};
} // namespace opendigitizer::test

#endif
