
#ifndef DATASOURCE_H
#define DATASOURCE_H

#include "../flowgraph.h"

namespace DigitizerUi {

class DataSource final : public Block {
public:
    explicit DataSource(std::string_view name);

    void        processData() final;
    static void registerBlockType(FlowGraph *fg);

private:
    std::vector<float> m_data;
    float              m_offset = 0;
};

} // namespace DigitizerUi

#endif
