
#ifndef DATASINK_H
#define DATASINK_H

#include <imgui.h>

#include "../flowgraph.h"

namespace DigitizerUi {

class DataSink final : public Block {
public:
    explicit DataSink(std::string_view name);

    void        processData() override;
    static void registerBlockType();

    bool        hasData = false;
    DataType    dataType;
    DataSet     data;

    ImVec4      color;

private:
};

class DataSinkSource final : public Block {
public:
    explicit DataSinkSource(std::string_view name);

    void        processData() override;
    static void registerBlockType();

private:
};

} // namespace DigitizerUi

#endif
