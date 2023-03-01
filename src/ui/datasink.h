
#ifndef DATASINK_H
#define DATASINK_H

#include <imgui.h>

#include "flowgraph.h"

namespace DigitizerUi {

class DataSink : public Block {
public:
    explicit DataSink(std::string_view name);

    void     processData() override;

    bool     hasData = false;
    DataType dataType;
    DataSet  data;

    ImVec4   color;

private:
};

} // namespace DigitizerUi

#endif
