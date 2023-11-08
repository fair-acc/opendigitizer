#include <majordomo/base64pp.hpp>
#include <majordomo/RestBackend.hpp>

using namespace opencmw::majordomo;

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
        _svr.set_mount_point("/", _serverRoot.string());

        _svr.Post("/stdio.html", [](const httplib::Request & /*request*/, httplib::Response &response) {
            response.set_content("", "text/plain");
        });

        auto cmrcHandler = [this](const httplib::Request &request, httplib::Response &response) {
            if (super_t::_vfs.is_file(request.path)) {
                // headers required for using the SharedArrayBuffer
                response.set_header("Cross-Origin-Opener-Policy", "same-origin");
                response.set_header("Cross-Origin-Embedder-Policy", "require-corp");
                // webworkers and wasm can only be executed if they have the correct mimetype
                std::string contentType;
                if (request.path.ends_with(".js")) {
                    contentType = "application/javascript";
                } else if (request.path.ends_with(".wasm")) {
                    contentType = "application/wasm";
                } else if (request.path.ends_with(".html")) {
                    contentType = "text/html";
                }
                auto file = super_t::_vfs.open(request.path);
                response.set_content(std::string(file.begin(), file.end()), contentType);
            } else {
            }
        };

        _svr.Get("/assets/.*", cmrcHandler);
        _svr.Get("/web/.*", cmrcHandler);

        // Register default handlers
        super_t::registerHandlers();
    }
};
