#pragma once

#ifndef __EMSCRIPTEN__

#include <ClientCommon.hpp>
#include <ClientContext.hpp>
#include <majordomo/Cryptography.hpp>

#include <Client.hpp>
#include <services/OAuthClient.hpp>

namespace gr::od {
struct OAuthSession {
    const opencmw::zmq::Context    _zctx;
    opencmw::client::ClientContext _clientContext = [&]() {
        std::vector<std::unique_ptr<opencmw::client::ClientBase>> clients;
        clients.emplace_back(std::make_unique<opencmw::client::MDClientCtx>(_zctx));
        return opencmw::client::ClientContext{std::move(clients)};
    }();

    std::string _secret;
    std::string _accessToken;
    std::string _roles;

    opencmw::majordomo::cryptography::PrivateKey _privateKey;
    opencmw::majordomo::cryptography::PublicKey  _publicKey;
    opencmw::majordomo::cryptography::KeyHash    _publicHash; // cached so that we do not have to recompute it

    static OAuthSession& instance();
    void                 signIn(const std::string& scope, const std::string& clientid, const std::string& endpoint);
    const std::string&   availableRoles() const;
    void                 sign(opencmw::mdp::Message& msg);
};
} // namespace gr::od

#endif
