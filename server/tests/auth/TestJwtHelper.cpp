#include "TestJwtHelper.hpp"

#include <stdexcept>
#include <vector>

#include <openssl/pem.h>
#include <openssl/rsa.h>

namespace test_helpers {

namespace {

std::string base64UrlEncode(const unsigned char* data, size_t len) {
    std::vector<unsigned char> buffer(4 * ((len + 2) / 3) + 1);
    const int written = EVP_EncodeBlock(buffer.data(), data, static_cast<int>(len));

    std::string encoded(reinterpret_cast<char*>(buffer.data()), static_cast<size_t>(written));
    for (auto& c : encoded) {
        if (c == '+') {
            c = '-';
        } else if (c == '/') {
            c = '_';
        }
    }
    while (!encoded.empty() && encoded.back() == '=') {
        encoded.pop_back();
    }
    return encoded;
}

std::string base64UrlEncode(const std::string& data) {
    return base64UrlEncode(reinterpret_cast<const unsigned char*>(data.data()), data.size());
}

} // namespace

TestRsaKeyPair::TestRsaKeyPair() {
    key = EVP_RSA_gen(2048);
    if (!key) {
        throw std::runtime_error("TestRsaKeyPair: failed to generate RSA key");
    }
}

TestRsaKeyPair::~TestRsaKeyPair() {
    if (key) {
        EVP_PKEY_free(key);
    }
}

std::string TestRsaKeyPair::publicKeyPem() const {
    BIO* bio = BIO_new(BIO_s_mem());
    PEM_write_bio_PUBKEY(bio, key);

    char* data = nullptr;
    const long len = BIO_get_mem_data(bio, &data);
    std::string pem(data, static_cast<size_t>(len));

    BIO_free(bio);
    return pem;
}

std::string signTestJwt(EVP_PKEY* private_key, const nlohmann::json& header, const nlohmann::json& payload) {
    const std::string header_b64 = base64UrlEncode(header.dump());
    const std::string payload_b64 = base64UrlEncode(payload.dump());
    const std::string signing_input = header_b64 + "." + payload_b64;

    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    EVP_PKEY_CTX* pctx = nullptr;
    EVP_DigestSignInit(mdctx, &pctx, EVP_sha256(), nullptr, private_key);
    EVP_PKEY_CTX_set_rsa_padding(pctx, RSA_PKCS1_PADDING);

    size_t sig_len = 0;
    EVP_DigestSign(mdctx, nullptr, &sig_len,
                   reinterpret_cast<const unsigned char*>(signing_input.data()), signing_input.size());

    std::vector<unsigned char> signature(sig_len);
    EVP_DigestSign(mdctx, signature.data(), &sig_len,
                   reinterpret_cast<const unsigned char*>(signing_input.data()), signing_input.size());
    signature.resize(sig_len);

    EVP_MD_CTX_free(mdctx);

    return signing_input + "." + base64UrlEncode(signature.data(), signature.size());
}

} // namespace test_helpers
