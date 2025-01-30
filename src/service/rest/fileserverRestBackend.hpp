#include <majordomo/RestBackend.hpp>
#include <majordomo/base64pp.hpp>
#include <settings.hpp>

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

    FileServerRestBackend(Broker<Roles...>& broker, const VirtualFS& vfs, std::filesystem::path serverRoot, opencmw::URI<> restAddress = opencmw::URI<>::factory().scheme(DEFAULT_REST_SCHEME).hostName("0.0.0.0").port(DEFAULT_REST_PORT).build()) : super_t(broker, vfs, restAddress), _serverRoot(std::move(serverRoot)) {}

    void registerHandlers() override {
        _svr.Post("/stdio.html", [](const httplib::Request& /*request*/, httplib::Response& response) { response.set_content("", "text/plain"); });

        auto contentTypeForFilename = [](const auto& path) {
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

        auto cmrcHandler = [this, contentTypeForFilename](const httplib::Request& request, httplib::Response& response) {
            response.set_header("Cross-Origin-Opener-Policy", "same-origin");
            response.set_header("Cross-Origin-Embedder-Policy", "require-corp");

            auto       path        = request.path;
            const auto contentType = contentTypeForFilename(path);

            if (path.empty()) {
                path = "index.html";
            }

            std::string_view trimmedPath(std::ranges::find_if_not(path, [](char c) { return c == '/'; }), path.cend());

            if (super_t::_vfs.is_file(path)) { // file embedded with cmakerc
                // headers required for using the SharedArrayBuffer
                response.set_header("Cache-Control", "public, max-age=3600"); // cache all artefacts for 1h
                // webworkers and wasm can only be executed if they have the correct mimetype
                auto file = super_t::_vfs.open(path);
                response.set_content(std::string(file.begin(), file.end()), contentType);

            } else if (auto filePath = _serverRoot / trimmedPath; sfs::exists(filePath)) { // read file from filesystem
                std::ifstream  file(filePath, std::ios::binary | std::ios::ate);
                std::string    data;
                std::streampos filesize = file.tellg();
                data.resize(static_cast<unsigned long>(filesize));
                file.seekg(0, std::ios::beg);
                file.read(data.data(), filesize);
                response.set_header("Cache-Control", "public, max-age=3600"); // cache all artefacts for 1h
                response.set_content(std::move(data), contentType);
            } else {
                std::cerr << "File not found: " << _serverRoot / request.path << std::endl;
                response.status = httplib::StatusCode::NotFound_404;
                response.set_content("Not found", "text/plain");
            }
        };
        _svr.Get("/assets/.*", cmrcHandler);
        _svr.Get("/web/.*", cmrcHandler);

        auto redirectHandler = [this](const httplib::Request& request, httplib::Response& response) {
            auto& settings = Digitizer::Settings::instance();
            response.set_redirect(fmt::format("/web/index.html#dashboard={}{}", settings.defaultDashboard, settings.darkMode ? "&darkMode=true" : ""));
        };
        _svr.Get("/", redirectHandler);
        _svr.Get("/index.html", redirectHandler);

        // Register default handlers
        super_t::registerHandlers();
    }
};
