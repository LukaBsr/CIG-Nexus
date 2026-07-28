#ifndef CIG_NEXUS_TESTS_AUTH_TEST_JWT_HELPER_HPP
#define CIG_NEXUS_TESTS_AUTH_TEST_JWT_HELPER_HPP

#include <string>

#include <nlohmann/json.hpp>
#include <openssl/evp.h>

namespace test_helpers {

// RAII wrapper so tests don't have to remember EVP_PKEY_free.
struct TestRsaKeyPair {
    TestRsaKeyPair();
    ~TestRsaKeyPair();

    TestRsaKeyPair(const TestRsaKeyPair&) = delete;
    TestRsaKeyPair& operator=(const TestRsaKeyPair&) = delete;

    EVP_PKEY* key = nullptr;
    std::string publicKeyPem() const;
};

// Signs an arbitrary header/payload with the given private key — used to
// build both well-formed test tokens and deliberately malformed ones (wrong
// alg, expired, wrong audience) for JwtVerifier's tests.
std::string signTestJwt(EVP_PKEY* private_key, const nlohmann::json& header, const nlohmann::json& payload);

} // namespace test_helpers

#endif // CIG_NEXUS_TESTS_AUTH_TEST_JWT_HELPER_HPP
