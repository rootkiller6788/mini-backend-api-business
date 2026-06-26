# mini-auth-security — Authentication & Security Framework (C99)

A comprehensive C99 authentication, authorization, and web security library
providing nine core modules: OAuth2 server, JWT handling, RBAC engine,
SSO federation, rate limiting, cryptographic utilities, audit logging,
session management, and web security protections.

**include/ + src/ = 4,596 lines** | **48 tests pass** | **make test passes cleanly**

---

## Module Status: COMPLETE ✅

| Level | Name | Status | Evidence |
|-------|------|--------|----------|
| **L1** | Definitions | **Complete** | 9 header files with struct/typedef/enum/API declarations |
| **L2** | Core Concepts | **Complete** | HMAC, PBKDF2, hash chains, session lifecycle, CSRF, XSS, CSP |
| **L3** | Engineering Structures | **Complete** | RBAC hierarchy, OAuth2 token store, SSO ticket model, hash-chain audit |
| **L4** | Standards/Theorems | **Complete** | HMAC-SHA256 (RFC 2104), PBKDF2 (RFC 2898), constant-time compare (Kocher 1996), OWASP Session Management, CSP Level 2 (W3C), NIST SP 800-63B/800-92 |
| **L5** | Algorithms/Methods | **Complete** | FNV-1a hash, xorshift128+ PRNG, token bucket/sliding window rate limit, PBKDF2 key derivation, HTML entity encoding |
| **L6** | Canonical Problems | **Complete** | Web authentication server, RBAC permission resolution, session validation, OAuth2 authorization code flow |
| **L7** | Applications | **Complete** (8 apps) | Password hashing (MCF), audit logging (SIEM JSON export), session management (idle timeout), CSRF protection (synchronizer token), CSP generation, security headers, email validation, XSS sanitization |
| **L8** | Advanced Topics | **Complete** (2 topics) | Hash-chain tamper detection (Merkle-Damgard style), CSP nonce-based policy |
| **L9** | Industry Frontiers | **Partial** | Documented: Argon2 migration path, FIDO2/WebAuthn conceptual notes (see docs/) |

---

## Features

| Module | File | Lines | Description |
|--------|------|-------|-------------|
| **OAuth2 Server** | `oauth2_server.{h,c}` | 357 | Auth code flow, client credentials, refresh token rotation, token revocation |
| **JWT Auth** | `jwt_auth.{h,c}` | 444 | HS256/RS256, standard claims, base64url encode/decode, token validation |
| **RBAC Engine** | `rbac_engine.{h,c}` | 410 | Role hierarchy, 64-bit bitmask permissions, cached resolution |
| **SSO Model** | `sso_model.{h,c}` | 434 | SAML 2.0 assertion generation, ticket/token exchange, session federation |
| **Rate Limiter** | `rate_limiter.{h,c}` | 361 | Fixed/sliding window, token bucket, Redis/Lua simulation, rate headers |
| **Crypto Utils** | `crypto_utils.{h,c}` | 542 | SHA-256, HMAC-SHA256, PBKDF2, constant-time compare, password hashing (MCF), secure buffers |
| **Audit Log** | `audit_log.{h,c}` | 480 | Structured event logging, hash-chain tamper detection, SIEM JSON export |
| **Session Manager** | `session_manager.{h,c}` | 677 | Session lifecycle, idle timeout, fixation prevention, attribute store |
| **Web Security** | `web_security.{h,c}` | 891 | CSRF tokens, XSS sanitization (5 contexts), CSP generation, security headers, input validation |

---

## Knowledge Level Details

### L1 — Core Definitions
- `JwtAlgorithm`, `JwtClaims`, `JwtSigningKey`, `JwtEngine` — JWT token model
- `RbacRole`, `RbacPermission`, `RbacUser`, `RbacEngine` — RBAC data model
- `OAuth2Client`, `OAuth2AuthCode`, `OAuth2TokenEntry`, `OAuth2Server` — OAuth2 entities
- `SsoIdentityProvider`, `SsoServiceProvider`, `SsoTicket`, `SsoSession` — SSO model
- `RateLimitEntry`, `RateLimiter`, `RateLimitResult` — Rate limiting
- `CryptoKdfConfig`, `CryptoPasswordHash`, `CryptoSecureBuffer` — Crypto primitives
- `AuditRecord`, `AuditLog`, `AuditEventStats` — Audit framework
- `SessionEntry`, `SessionManager` — Session management
- `WsCsrfToken`, `WsCspPolicy`, `WsSecurityHeaders` — Web security

### L4 — Standards & Theorems
| Standard | Implementation | File |
|----------|---------------|------|
| RFC 2104 (HMAC) | `crypto_hmac_sha256()` | `crypto_utils.c` |
| RFC 2898 (PBKDF2) | `crypto_pbkdf2()` | `crypto_utils.c` |
| RFC 4648 (Base64) | `crypto_base64_encode/decode()` | `crypto_utils.c` |
| RFC 6749 (OAuth2) | Auth code + client credentials flows | `oauth2_server.c` |
| RFC 7519 (JWT) | JWT encode/decode/validate | `jwt_auth.c` |
| Kocher 1996 (Timing Attacks) | `crypto_const_time_compare()` | `crypto_utils.c` |
| OWASP Session Mgmt | Fixation prevention, idle timeout | `session_manager.c` |
| OWASP XSS Prevention | 5-context sanitization | `web_security.c` |
| CSP Level 2 (W3C) | Nonce-based CSP generation | `web_security.c` |
| NIST SP 800-63B | Salt generation, PBKDF2 parameters | `crypto_utils.c` |
| NIST SP 800-92 | Structured audit logging | `audit_log.c` |

### L5 — Key Algorithms
1. **PBKDF2-HMAC-SHA256** — Iterated HMAC key derivation (RFC 2898 §5.2)
   - `T_i = U_1 ⊕ U_2 ⊕ ... ⊕ U_c` where `U_j = PRF(Password, U_{j-1})`
   - Complexity: O(c · dkLen)
2. **Hash-chain tamper detection** — Merkle-Damgard style linking
   - `H_i = FNV1a(Record_i || H_{i-1})`
   - Any modification breaks all subsequent links
3. **Token bucket rate limiting** — Continuous refill model
   - `tokens = min(max_tokens, tokens + elapsed × refill_rate)`
4. **Constant-time comparison** — XOR accumulation without early exit
   - `diff |= a[i] ⊕ b[i]` for all i, regardless of differences found
5. **HTML entity encoding** — Context-aware XSS prevention (5 modes)

### L6 — Canonical Problems
- **Web authentication server** — OAuth2 authorization code flow with token exchange
- **Role-based access control** — Hierarchical RBAC with inheritance and caching
- **Session lifecycle management** — Create → validate → extend → terminate → cleanup
- **Rate-limited API gateway** — Multi-algorithm rate limiting per user/IP/API scope

### L7 — Applications (8 implementations)
1. Password hashing with Modular Crypt Format (`$pbkdf2-sha256$iter$salt$hash`)
2. Security audit logging with SIEM-compatible JSON export
3. Session management with idle timeout and fixation prevention
4. CSRF synchronizer token pattern
5. Content Security Policy header generation with nonce support
6. OWASP security headers bundle (HSTS, X-Frame-Options, etc.)
7. Email format validation (RFC 5322 simplified)
8. XSS sanitization across HTML, JavaScript, URL, CSS, and HTML-attribute contexts

### L8 — Advanced Topics (2 implementations)
1. **Hash-chain tamper detection** — Immutable audit trail via cryptographic linking
   - Each record contains `prev_hash` → forms append-only log
   - Verification recomputes all hashes; any mismatch indicates tampering
2. **CSP nonce-based policy** — Per-request random nonces for inline script/style
   - Nonce must be: unique per request, cryptographically random, base64-encoded
   - Enables inline scripts without `'unsafe-inline'` (maintains XSS protection)

### L9 — Industry Frontiers (Documented)
- Argon2id migration path: PBKDF2 → Argon2 (memory-hard KDF)
- FIDO2/WebAuthn: passwordless authentication with public-key crypto
- Confidential computing: TEE-based secret management (Intel SGX, AMD SEV)
- See `docs/doc_design.md` for detailed discussion

---

## Nine-School Curriculum Mapping

| School | Course | This Module Covers |
|--------|--------|--------------------|
| **MIT** | 6.858 Computer Security | OAuth2, JWT, RBAC, CSRF/XSS, constant-time crypto |
| **Stanford** | CS 142 Web Applications | Session mgmt, CSRF, XSS, CSP, security headers |
| **Berkeley** | CS 161 Computer Security | Password hashing, audit logging, timing attacks |
| **CMU** | 15-410 Operating Systems | Secure buffers (mlock), privilege separation |
| **UT Austin** | CS 380D Distributed Systems | OAuth2 distributed auth, rate limiting |
| **ETH** | 263-4640 System Security | Formal reasoning about hash chains, CSP policy |
| **Cambridge** | Part II: Security | Protocol design (OAuth2 flows), attack modeling |
| **清华** | 网络与信息安全 | 完整认证授权框架, 审计追踪 |
| **Georgia Tech** | CS 6262 Network Security | Web security protections, input validation |

---

## Project Structure

```
mini-auth-security/
├── Makefile                       # make all / make test / make clean
├── README.md                      # This file
├── include/                       # 9 header files (1,074 lines)
│   ├── oauth2_server.h            OAuth2 authorization server
│   ├── jwt_auth.h                 JWT token handling
│   ├── rbac_engine.h              RBAC permission engine
│   ├── sso_model.h                SSO federation model
│   ├── rate_limiter.h             Rate limiting algorithms
│   ├── crypto_utils.h             Cryptographic utilities
│   ├── audit_log.h                Security audit logging
│   ├── session_manager.h          Centralized session management
│   └── web_security.h             Web security protections
├── src/                           # 9 implementation files (3,522 lines)
│   ├── oauth2_server.c
│   ├── jwt_auth.c
│   ├── rbac_engine.c
│   ├── sso_model.c
│   ├── rate_limiter.c
│   ├── crypto_utils.c
│   ├── audit_log.c
│   ├── session_manager.c
│   └── web_security.c
├── tests/
│   └── test_all.c                 # 48 tests covering all 9 modules
├── examples/
│   ├── example_oauth2.c           OAuth2 authorization code flow
│   ├── example_jwt.c              JWT encode/decode/validate
│   └── example_rbac.c             RBAC permission resolution
├── demos/
│   ├── demo_sso.c                 SSO authentication flow
│   └── demo_rate_limiter.c        Rate limiting algorithm comparison
├── docs/
│   ├── doc_api.md                 Complete API reference
│   └── doc_design.md              Architecture and design decisions
├── benches/                       (Performance benchmarks)
└── build/                         (Build artifacts)
```

## Build & Test

### Prerequisites

```bash
# Debian/Ubuntu
sudo apt install libssl-dev
# macOS
brew install openssl
# Windows (MSYS2)
pacman -S mingw-w64-x86_64-openssl
```

### Build All

```bash
make all          # Build library, examples, and demos
make lib          # Build static library only (libminiauth.a)
make examples     # Build example programs
make demos        # Build demo programs
```

### Run Tests

```bash
make test         # Compile and run 48 tests across all modules
```

Expected output:
```
=== mini-auth-security Test Suite ===
[crypto_utils]    12 tests PASSED
[audit_log]        5 tests PASSED
[session_manager]  6 tests PASSED
[web_security]    13 tests PASSED
[rate_limiter]     3 tests PASSED
[rbac_engine]      4 tests PASSED
[jwt_auth]         2 tests PASSED
[oauth2_server]    2 tests PASSED
[sso_model]        1 test  PASSED
========================================
RESULTS: 48/48 tests passed
========================================
```

## Cross-Module Integration

- **backend(8) → security(13)**: Session manager and audit log can be integrated into `mini-backend-framework` for request-level auth and logging
- **network(5) → security(13)**: Rate limiter consumes network-layer IP/connection data
- **security(13) → data-engine(7)**: Password hashes stored via MCF format; audit log exports JSON

## License

MIT
