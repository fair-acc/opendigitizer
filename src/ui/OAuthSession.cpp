#include "OAuthSession.hpp"

#ifndef __EMSCRIPTEN__

namespace gr::od {

OAuthSession& OAuthSession::instance() {
    static OAuthSession session;
    return session;
}

void OAuthSession::signIn(const std::string& scope, const std::string& clientid, const std::string& endpoint) {
    if (_secret.empty()) {
        opencmw::IoBuffer   inBuf;
        opencmw::OAuthInput in;
        in.scope                          = scope;
        in.clientId                       = clientid;
        std::tie(_publicKey, _privateKey) = opencmw::majordomo::cryptography::generateKeyPair();
        _publicHash                       = opencmw::majordomo::cryptography::publicKeyHash(_publicKey);
        in.publicKey                      = std::string(_publicKey.key, _publicKey.key + crypto_sign_PUBLICKEYBYTES);
        opencmw::serialise<opencmw::YaS>(inBuf, in);
        _clientContext.set(
            opencmw::URI(endpoint),
            [&](const opencmw::mdp::Message& rep) {
                opencmw::OAuthOutput tokenOut;
                auto                 tokenOutBuf = rep.data;
                opencmw::deserialise<opencmw::YaS, opencmw::ProtocolCheck::IGNORE>(tokenOutBuf, tokenOut);
                _secret = tokenOut.secret;
                opencmw::OAuthClient::openWebBrowser(opencmw::URI(tokenOut.authorizationUri));
            },
            std::move(inBuf));
        while (_secret.empty()) {
            sleep(1);
        }
    }
    if (_accessToken.empty()) {
        opencmw::IoBuffer   inBuf;
        opencmw::OAuthInput in;
        in.scope    = scope;
        in.clientId = clientid;
        in.secret   = _secret;
        opencmw::serialise<opencmw::YaS>(inBuf, in);
        _clientContext.set(
            opencmw::URI(endpoint),
            [&](const opencmw::mdp::Message& rep) {
                opencmw::OAuthOutput tokenOut;
                auto                 tokenOutBuf = rep.data;
                opencmw::deserialise<opencmw::YaS, opencmw::ProtocolCheck::IGNORE>(tokenOutBuf, tokenOut);
                _accessToken = tokenOut.accessToken;
                _roles       = tokenOut.roles;
            },
            std::move(inBuf));
    }
}

const std::string& OAuthSession::availableRoles() const { return _roles; }

void OAuthSession::sign(opencmw::mdp::Message& msg) { opencmw::majordomo::cryptography::sign(msg, _privateKey, _publicHash); }

} // namespace gr::od

#endif
