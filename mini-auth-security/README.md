# mini-auth-security — 认证与安全 (C 语言实现)

A compact C99 authentication and authorization library providing five core security modules:
OAuth2 authorization server, JWT token handling, RBAC permission engine,
SSO federation model, and rate limiting.

## Features

- **OAuth2 Server**: Authorization code flow, client credentials flow, refresh token rotation, token revocation
- **JWT Auth**: HS256/RS256 signing/verification, standard claims (iss/sub/aud/exp/iat/nbf), token refresh
- **RBAC Engine**: Role hierarchy (ADMIN > MODERATOR > EDITOR > VIEWER), 64-bit bitmask permissions, cached resolution
- **SSO Model**: Central auth service, ticket/token exchange, SAML 2.0 assertion generation, session federation
- **Rate Limiter**: Fixed window, sliding window, token bucket algorithms, Redis/Lua distributed simulation, rate limit headers

## Project Structure

```
mini-auth-security/
├── include/
│   ├── oauth2_server.h      OAuth2 authorization server
│   ├── jwt_auth.h            JWT token handling
│   ├── rbac_engine.h         RBAC permission engine
│   ├── sso_model.h           SSO federation model
│   └── rate_limiter.h        Rate limiting
├── src/
│   ├── oauth2_server.c
│   ├── jwt_auth.c
│   ├── rbac_engine.c
│   ├── sso_model.c
│   └── rate_limiter.c
├── examples/
│   ├── example_oauth2.c      OAuth2 flow demonstration
│   ├── example_jwt.c         JWT encode/decode/validate
│   └── example_rbac.c        RBAC permission checks
├── demos/
│   ├── demo_sso.c            SSO authentication flow
│   └── demo_rate_limiter.c   Rate limiting algorithms
├── docs/
│   ├── doc_api.md            API reference
│   └── doc_design.md         Design document
├── Makefile
└── README.md
```

## Build

```bash
make all
```

Requires OpenSSL development headers:

```bash
# Debian/Ubuntu
sudo apt install libssl-dev
# macOS
brew install openssl
# Windows (MSYS2)
pacman -S mingw-w64-x86_64-openssl
```

## Quick Start

### OAuth2

```c
#include "oauth2_server.h"

OAuth2Server server;
oauth2_server_init(&server);

oauth2_register_client(&server, "myapp", "secret", "https://app/cb", "read write", 1);

char access_token[OAUTH2_ACCESS_TOKEN_LEN];
oauth2_client_credentials_grant(&server, "myapp", "secret", "read",
                                access_token, sizeof(access_token));
```

### JWT

```c
#include "jwt_auth.h"

JwtEngine eng;
jwt_engine_init(&eng, JWT_ALG_HS256, (uint8_t *)"secret-key", 10);

JwtClaims claims;
jwt_claims_set_defaults(&claims);
strncpy(claims.iss, "auth.example.com", JWT_MAX_ISS_LEN - 1);
strncpy(claims.sub, "user-42", JWT_MAX_SUB_LEN - 1);

char token[JWT_MAX_TOKEN_LEN];
jwt_encode(&eng, &claims, token, sizeof(token));
```

### RBAC

```c
#include "rbac_engine.h"

RbacEngine eng;
rbac_engine_init(&eng);
rbac_setup_default_roles(&eng);

rbac_create_user(&eng, "alice");
rbac_assign_role(&eng, "alice", "ADMIN");

if (rbac_check_permission(&eng, "alice", PERM_DELETE_POSTS)) {
    // Allow
}
```

### Rate Limiter

```c
#include "rate_limiter.h"

RateLimiter rl;
rate_limiter_init(&rl, 100, 60);

RateLimitResult res = rate_limiter_check(&rl, "user:bob",
    RATE_SCOPE_USER, RATE_ALGO_TOKEN_BUCKET, 10, 60);

if (res.allowed) {
    // Process request
}
```

## API Documentation

See `docs/doc_api.md` for complete API reference.
See `docs/doc_design.md` for architecture and design decisions.

## License

MIT
