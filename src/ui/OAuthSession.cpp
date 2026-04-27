#include "OAuthSession.hpp"

#ifndef __EMSCRIPTEN__

namespace DigitizerUi {

OAuthSession& OAuthSession::instance() {
    static OAuthSession session;
    return session;
}

void OAuthSession::signIn(const std::string& scope, const std::string& clientid, const std::string& endpoint) {
    if (secret.empty()) {
        opencmw::IoBuffer   inBuf;
        opencmw::OAuthInput in{scope, clientid};
        std::tie(publicKey, privateKey) = opencmw::majordomo::cryptography::generateKeyPair();
        publicHash                      = opencmw::majordomo::cryptography::publicKeyHash(publicKey);
        in.publicKey                    = std::string(publicKey.key, publicKey.key + crypto_sign_PUBLICKEYBYTES);
        opencmw::serialise<opencmw::YaS>(inBuf, in);
        clientContext.set(
            opencmw::URI(endpoint),
            [&](const opencmw::mdp::Message& rep) {
                opencmw::OAuthOutput tokenOut;
                auto                 tokenOutBuf = rep.data;
                opencmw::deserialise<opencmw::YaS, opencmw::ProtocolCheck::IGNORE>(tokenOutBuf, tokenOut);
                secret = tokenOut.secret;
                opencmw::OAuthClient::openWebBrowser(opencmw::URI(tokenOut.authorizationUri));
            },
            std::move(inBuf));
        while (secret.empty()) {
            sleep(1);
        }
    }
    if (accessToken.empty()) {
        opencmw::IoBuffer   inBuf;
        opencmw::OAuthInput in{scope, clientid};
        in.secret = secret;
        opencmw::serialise<opencmw::YaS>(inBuf, in);
        clientContext.set(
            opencmw::URI(endpoint),
            [&](const opencmw::mdp::Message& rep) {
                opencmw::OAuthOutput tokenOut;
                auto                 tokenOutBuf = rep.data;
                opencmw::deserialise<opencmw::YaS, opencmw::ProtocolCheck::IGNORE>(tokenOutBuf, tokenOut);
                accessToken = tokenOut.accessToken;
                roles       = tokenOut.roles;
            },
            std::move(inBuf));
    }
}

const std::string& OAuthSession::availableRoles() const { return roles; }

void OAuthSession::sign(opencmw::mdp::Message& msg) { opencmw::majordomo::cryptography::sign(msg, privateKey, publicHash); }

} // namespace DigitizerUi

#endif
