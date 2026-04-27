#pragma once

#ifndef __EMSCRIPTEN__

#include <ClientCommon.hpp>
#include <ClientContext.hpp>
#include <majordomo/Cryptography.hpp>

#include <Client.hpp>
#include <services/OAuthClient.hpp>

namespace DigitizerUi {
class OAuthSession {
public:
    static OAuthSession& instance();
    void                 signIn(const std::string& scope, const std::string& clientid, const std::string& endpoint);
    const std::string&   availableRoles() const;
    void                 sign(opencmw::mdp::Message& msg);

private:
    const opencmw::zmq::Context    zctx;
    opencmw::client::ClientContext clientContext = [&]() {
        std::vector<std::unique_ptr<opencmw::client::ClientBase>> clients;
        clients.emplace_back(std::make_unique<opencmw::client::MDClientCtx>(zctx));
        return opencmw::client::ClientContext{std::move(clients)};
    }();

    std::string secret;
    std::string accessToken;
    std::string roles;

    opencmw::majordomo::cryptography::PrivateKey privateKey;
    opencmw::majordomo::cryptography::PublicKey  publicKey;
    opencmw::majordomo::cryptography::KeyHash    publicHash; // cached so that we do not have to recompute it
};
} // namespace DigitizerUi

#endif
