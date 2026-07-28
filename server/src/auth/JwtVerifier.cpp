#include "auth/JwtVerifier.hpp"

#include <array>
#include <chrono>
#include <initializer_list>
#include <stdexcept>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>

namespace auth {

namespace {

constexpr const char* kExpectedAudience = "cig-nexus-server";

std::optional<std::vector<unsigned char>> base64UrlDecode(const std::string& input) {
    static const std::string alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

    static const auto reverse_lookup = [] {
        std::array<int, 256> table{};
        table.fill(-1);
        for (size_t i = 0; i < alphabet.size(); ++i) {
            table[static_cast<unsigned char>(alphabet[i])] = static_cast<int>(i);
        }
        return table;
    }();

    std::vector<unsigned char> output;
    int accumulator = 0;
    int bits = -8;

    for (unsigned char c : input) {
        const int value = reverse_lookup[c];
        if (value == -1) {
            return std::nullopt;
        }

        accumulator = (accumulator << 6) + value;
        bits += 6;
        if (bits >= 0) {
            output.push_back(static_cast<unsigned char>((accumulator >> bits) & 0xFF));
            bits -= 8;
        }
    }

    return output;
}

std::vector<std::string> splitJwt(const std::string& token) {
    std::vector<std::string> parts;
    size_t start = 0;
    for (size_t i = 0; i <= token.size(); ++i) {
        if (i == token.size() || token[i] == '.') {
            parts.push_back(token.substr(start, i - start));
            start = i + 1;
        }
    }
    return parts;
}

uint64_t nowSeconds() {
    const auto now = std::chrono::system_clock::now();
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count());
}

} // namespace

struct JwtVerifier::Impl {
    EVP_PKEY* public_key = nullptr;

    explicit Impl(const std::string& public_key_pem) {
        BIO* bio = BIO_new_mem_buf(public_key_pem.data(), static_cast<int>(public_key_pem.size()));
        if (!bio) {
            throw std::runtime_error("JwtVerifier: failed to allocate BIO for public key");
        }

        public_key = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
        BIO_free(bio);

        if (!public_key) {
            throw std::runtime_error("JwtVerifier: invalid RS256 public key PEM");
        }
    }

    ~Impl() {
        if (public_key) {
            EVP_PKEY_free(public_key);
        }
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
};

JwtVerifier::JwtVerifier(const std::string& public_key_pem)
    : impl_(std::make_unique<Impl>(public_key_pem)) {}

JwtVerifier::~JwtVerifier() = default;
JwtVerifier::JwtVerifier(JwtVerifier&&) noexcept = default;
JwtVerifier& JwtVerifier::operator=(JwtVerifier&&) noexcept = default;

JwtVerification JwtVerifier::verify(const std::string& token) const {
    const std::vector<std::string> parts = splitJwt(token);
    if (parts.size() != 3) {
        return {JwtVerifyResult::Malformed, std::nullopt};
    }

    const std::string& header_b64 = parts[0];
    const std::string& payload_b64 = parts[1];
    const std::string& signature_b64 = parts[2];

    const auto header_bytes = base64UrlDecode(header_b64);
    const auto signature_bytes = base64UrlDecode(signature_b64);
    if (!header_bytes || !signature_bytes) {
        return {JwtVerifyResult::Malformed, std::nullopt};
    }

    // The header alone is parsed before the signature is checked — this is
    // fine, since alg is itself part of the signed content and gets
    // re-validated implicitly by the signature check below (a tampered alg
    // fails verification like any other tampered byte). The *payload* is
    // deliberately NOT parsed yet: it's still unverified attacker-controlled
    // content at this point, and parsing it before the signature check
    // would mean a corrupted-but-still-signature-invalid payload could
    // report a JSON-parse failure (Malformed) instead of the actually
    // correct InvalidSignature — i.e. verify-then-parse, not parse-then-verify.
    nlohmann::json header;
    try {
        header = nlohmann::json::parse(header_bytes->begin(), header_bytes->end());
    } catch (const nlohmann::json::exception&) {
        return {JwtVerifyResult::Malformed, std::nullopt};
    }

    if (!header.is_object() || !header.contains("alg") || !header["alg"].is_string()) {
        return {JwtVerifyResult::Malformed, std::nullopt};
    }

    // Algorithm pinning: the header's "alg" is only ever compared for
    // equality against the one algorithm this verifier supports. It never
    // influences which verification routine runs below — that is always,
    // unconditionally, RS256/SHA-256 against impl_->public_key. A token
    // declaring any other algorithm (including "none") is rejected here,
    // before any cryptographic operation is attempted.
    if (header["alg"].get<std::string>() != "RS256") {
        return {JwtVerifyResult::UnsupportedAlgorithm, std::nullopt};
    }

    const std::string signing_input = header_b64 + "." + payload_b64;

    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    if (!mdctx) {
        return {JwtVerifyResult::InvalidSignature, std::nullopt};
    }

    EVP_PKEY_CTX* pctx = nullptr;
    bool init_ok =
        EVP_DigestVerifyInit(mdctx, &pctx, EVP_sha256(), nullptr, impl_->public_key) == 1;
    if (init_ok && pctx) {
        init_ok = EVP_PKEY_CTX_set_rsa_padding(pctx, RSA_PKCS1_PADDING) > 0;
    }

    bool signature_valid = false;
    if (init_ok) {
        const int rc = EVP_DigestVerify(
            mdctx, signature_bytes->data(), signature_bytes->size(),
            reinterpret_cast<const unsigned char*>(signing_input.data()), signing_input.size());
        signature_valid = rc == 1;
    }

    EVP_MD_CTX_free(mdctx);

    if (!signature_valid) {
        return {JwtVerifyResult::InvalidSignature, std::nullopt};
    }

    // Only decode/parse the payload once its signature is verified.
    const auto payload_bytes = base64UrlDecode(payload_b64);
    if (!payload_bytes) {
        return {JwtVerifyResult::Malformed, std::nullopt};
    }

    nlohmann::json payload;
    try {
        payload = nlohmann::json::parse(payload_bytes->begin(), payload_bytes->end());
    } catch (const nlohmann::json::exception&) {
        return {JwtVerifyResult::Malformed, std::nullopt};
    }

    if (!payload.is_object() || !payload.contains("exp") || !payload["exp"].is_number_integer()) {
        return {JwtVerifyResult::Malformed, std::nullopt};
    }

    const uint64_t exp = payload["exp"].get<uint64_t>();
    if (nowSeconds() >= exp) {
        return {JwtVerifyResult::Expired, std::nullopt};
    }

    if (!payload.contains("aud") || !payload["aud"].is_string() ||
        payload["aud"].get<std::string>() != kExpectedAudience) {
        return {JwtVerifyResult::WrongAudience, std::nullopt};
    }

    AccessJwtClaims claims;
    for (const auto& [key, target] : std::initializer_list<std::pair<const char*, std::string*>>{
             {"sub", &claims.sub},
             {"discord_id", &claims.discord_id},
             {"username", &claims.username},
             {"sid", &claims.sid}}) {
        if (!payload.contains(key) || !payload[key].is_string()) {
            return {JwtVerifyResult::Malformed, std::nullopt};
        }
        *target = payload[key].get<std::string>();
    }

    return {JwtVerifyResult::Ok, claims};
}

} // namespace auth
