#include <majordomo/base64pp.hpp>
#include <majordomo/RestBackend.hpp>

using namespace opencmw::majordomo;

namespace sfs = std::filesystem;

template<typename Mode, typename VirtualFS, role... Roles>
class FileServerRestBackend : public RestBackend<Mode, VirtualFS, Roles...> {
private:
    using super_t = RestBackend<Mode, VirtualFS, Roles...>;
    std::filesystem::path _serverRoot;
    using super_t::_svr;
    using super_t::DEFAULT_REST_SCHEME;

public:
    using super_t::RestBackend;

    FileServerRestBackend(Broker<Roles...> &broker, const VirtualFS &vfs, std::filesystem::path serverRoot, opencmw::URI<> restAddress = opencmw::URI<>::factory().scheme(DEFAULT_REST_SCHEME).hostName("0.0.0.0").port(DEFAULT_REST_PORT).build())
        : super_t(broker, vfs, restAddress), _serverRoot(std::move(serverRoot)) {
    }

    void registerHandlers() override {
        _svr.Post("/stdio.html", [](const httplib::Request & /*request*/, httplib::Response &response) {
            response.set_content("", "text/plain");
        });

        auto contentTypeForFilename = [](const auto &path) {
            std::string contentType;
            if (path.ends_with(".js")) {
                contentType = "application/javascript";
            } else if (path.ends_with(".wasm")) {
                contentType = "application/wasm";
            } else if (path.ends_with(".html")) {
                contentType = "text/html";
            }
            return contentType;
        };

        auto cmrcHandler = [this, contentTypeForFilename](const httplib::Request &request, httplib::Response &response) {
            response.set_header("Cross-Origin-Opener-Policy", "same-origin");
            response.set_header("Cross-Origin-Embedder-Policy", "require-corp");

            auto path = request.path;
            const auto contentType = contentTypeForFilename(path);

            if (path.empty()) {
                path = "index.html";
            }

            std::string_view trimmedPath(
                    std::ranges::find_if_not(path, [] (char c) { return c == '/'; }),
                    path.cend());
            std::cerr << "trimmed path " << trimmedPath << std::endl;

            if (super_t::_vfs.is_file(path)) {
                // headers required for using the SharedArrayBuffer
                // webworkers and wasm can only be executed if they have the correct mimetype
                auto file = super_t::_vfs.open(path);
                response.set_content(std::string(file.begin(), file.end()), contentType);

            } else if (auto filePath = _serverRoot / trimmedPath; sfs::exists(filePath)) {
                std::ifstream inFile(filePath);
                std::string   data;

                inFile.seekg(0, std::ios::end);
                data.reserve(inFile.tellg());
                inFile.seekg(0, std::ios::beg);

                data.assign(std::istreambuf_iterator<char>(inFile), std::istreambuf_iterator<char>());
                response.set_content(std::move(data), contentType);

            } else {
                std::cerr << "File not found: " << _serverRoot / request.path << std::endl;
                response.set_content("Not found", "text/plain");
            }
        };

        _svr.Get("/assets/.*", cmrcHandler);
        _svr.Get("/web/.*", cmrcHandler);

        // Register default handlers
        super_t::registerHandlers();
    }
};
