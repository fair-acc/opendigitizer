
#ifndef DATASOURCE_H
#define DATASOURCE_H

#include "../flowgraph.h"

namespace DigitizerUi {

class DataSource : public Block {
public:
    static BlockType *btype() {
        static auto *t = []() {
            static BlockType t("sinsource");
            t.outputs.resize(1);
            t.outputs[0].name = "out";
            t.outputs[0].type = "float";
            return &t;
        }();
        return t;
    }

    explicit DataSource(const char *name, float freq)
        : Block(name, "sinsource", btype())
        , m_freq(freq) {
        m_data.resize(8192);

        processData();
    }

    void processData() override {
        for (int i = 0; i < m_data.size(); ++i) {
            m_data[i] = std::sin((m_offset + i) * m_freq);
        }
        outputs()[0].dataSet = m_data;
        m_offset += 1;
    }

private:
    std::vector<float> m_data;
    float              m_freq;
    float              m_offset = 0;
};

} // namespace DigitizerUi

#endif
