#ifndef CIG_NEXUS_AUTH_JWT_VERIFIER_HPP
#define CIG_NEXUS_AUTH_JWT_VERIFIER_HPP

#include <memory>
#include <optional>
#include <string>

namespace auth {

struct AccessJwtClaims {
    std::string sub; // "u_<uuid>"
    std::string discord_id;
    std::string username;
    std::string sid; // sessions.id — used by the revocation cache
};

enum class JwtVerifyResult {
    Ok,
    Malformed,            // not well-formed JWT, or missing/wrong-typed claims
    UnsupportedAlgorithm, // header "alg" is not exactly "RS256"
    InvalidSignature,
    Expired,
    WrongAudience
};

struct JwtVerification {
    JwtVerifyResult result;
    std::optional<AccessJwtClaims> claims; // set iff result == Ok
};

// Verifies design doc §6 access JWTs. The verifier is configured with a
// single RS256 public key and *always* performs RS256/SHA-256 signature
// verification against it — the token's own header "alg" field is checked
// for equality against "RS256" as a fast-reject, but it never selects which
// verification routine runs. This is the algorithm-pinning requirement from
// §6/§9: a verifier that instead branched on the header's declared
// algorithm would be vulnerable to algorithm-confusion attacks (including
// "alg: none"). See docs/auth-discord-design.md §9.1 checklist.
class JwtVerifier {
  public:
    // public_key_pem: an RS256 SPKI PEM public key (jose's importSPKI /
    // Next.js's SESSION_JWT_PRIVATE_KEY_PATH counterpart, design doc
    // §6/§8.2's AUTH_JWT_PUBLIC_KEY_PATH — read from disk by main.cpp
    // before this constructor is ever called).
    explicit JwtVerifier(const std::string& public_key_pem);
    ~JwtVerifier();

    JwtVerifier(const JwtVerifier&) = delete;
    JwtVerifier& operator=(const JwtVerifier&) = delete;
    JwtVerifier(JwtVerifier&&) noexcept;
    JwtVerifier& operator=(JwtVerifier&&) noexcept;

    JwtVerification verify(const std::string& token) const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace auth

#endif // CIG_NEXUS_AUTH_JWT_VERIFIER_HPP
