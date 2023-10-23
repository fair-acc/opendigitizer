#ifndef REMOTEDATASOURCE_H
#define REMOTEDATASOURCE_H

#include "../flowgraph.h"

namespace DigitizerUi {

class RemoteBlockType;

class RemoteDataSource final : public Block {
public:
    RemoteDataSource(std::string_view name, RemoteBlockType *t);
    ~RemoteDataSource();

    // void        processData() final;
    std::unique_ptr<gr::BlockModel> createGraphNode() final;
    static void                     registerBlockType(FlowGraph *fg, std::string_view path);
    static void                     registerBlockType(FlowGraph *fg, std::string_view path, std::string_view signalName);

private:
    RemoteBlockType *m_type;
};

} // namespace DigitizerUi

#endif
