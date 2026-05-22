# mini-auth-security Design Document

## Overview

mini-auth-security is a pure C99 authentication and authorization library providing
five core security modules: OAuth2 authorization server, JWT token handling,
RBAC permission engine, SSO federation model, and rate limiting.

## Architecture

```
+------------------------------------------------------------+
|                     Application Layer                       |
+------------------------------------------------------------+
        |            |            |            |
   OAuth2         JWT         RBAC          SSO       RateLimiter
   Server        Engine       Engine        Model
        |            |            |            |
+------------------------------------------------------------+
|                     Core Primitives                         |
|  HMAC-SHA256  |  Base64URL  |  Bitmask  |  XML Gen         |
+------------------------------------------------------------+
```

## Module Design

### 1. OAuth2 Server

- **Auth Code Flow**: Browser-based apps. Redirect to auth endpoint, receive code,
  exchange for tokens.
- **Client Credentials**: Server-to-server. Direct token issuance.
- **Refresh Token**: Long-lived credential for obtaining new access tokens.
- **Token Storage**: In-memory array with creation/expiry timestamps.
- **Security**: Confidential clients require secret validation. Auth codes are
  single-use with 10-minute TTL.

### 2. JWT Auth

- **Signing Algorithms**: HS256 (symmetric) via OpenSSL HMAC. RS256 (asymmetric)
  simulated with HMAC for portability.
- **Claims**: Standard JWT registered claims (iss, sub, aud, exp, iat, nbf, jti)
  plus custom scope claim.
- **Encoding**: JSON header + payload, Base64URL encoded, HMAC signature appended.
- **Validation Pipeline**: Decode -> Verify signature -> Check expiry -> Check issuer
  -> Check audience. Fail fast on first mismatch.
- **Token Refresh**: Decode existing valid token, update iat/exp, re-encode.

### 3. RBAC Engine

- **Permission Model**: Flat bitmask (64-bit uint64_t). 64 possible permissions.
  Defined by resource:action pairs.
- **Role Hierarchy**: Tree structure via parent role references. Permissions
  inherit upward through the hierarchy (depth-limited to 10 to prevent cycles).
- **Caching**: Each user caches their resolved effective permission mask.
  Cache invalidated on role assignment/removal.
- **Default Setup**: Four-tier hierarchy (VIEWER < EDITOR < MODERATOR < ADMIN)
  with increasing permissions at each level.
- **Multi-Check**: Support for ALL mode (every permission required) and ANY mode
  (at least one permission required).

### 4. SSO Model

- **Federation**: Single IDP, multiple SPs. Trust relationship must be explicitly
  established before authentication.
- **Ticket-Based Exchange**: Tickets are one-time-use, short-lived (5 min),
  consumed on first validation. Prevents replay attacks.
- **SAML Assertion**: XML generation following SAML 2.0 core schema. Includes
  Issuer, Subject/NameID, Conditions with NotBefore/NotOnOrAfter, AudienceRestriction,
  and AuthnStatement.
- **Session Management**: Sessions have configurable expiry (default 1h),
  last-accessed tracking, and explicit logout support.
- **Protocols**: SAML2, OIDC, CAS (defined as enums, SAML2 implemented).

### 5. Rate Limiter

- **Fixed Window**: Simple counter with wall-clock boundary. Resets fully at
  window edge. Risk of double-burst at boundary.
- **Sliding Window**: Timestamp ring buffer. Gradual decay. Smoother than fixed
  window at cost of O(n) lookup.
- **Token Bucket**: Continuous refill model. Best for bursty traffic with
  configurable fill rate.
- **Scopes**: Per-user (authentication context), per-IP (network layer),
  per-API (endpoint-specific), global (system-wide).
- **Distributed Simulation**: `rate_redis_sim_acquire` simulates a Redis-based
  distributed counter with atomic INCR + EXPIRE.
- **HTTP Headers**: Standard `X-RateLimit-Limit`, `X-RateLimit-Remaining`,
  `X-RateLimit-Reset` headers for client awareness.

## Data Flow: Typical Request

```
Client Request
    |
    v
[Rate Limiter] --> Check per-user, per-IP limits
    |                  |
    v                  v
[JWT Validation] --> Decode + Verify signature
    |                  |
    v                  v
[RBAC Check]     --> Verify permissions for endpoint
    |
    v
[OAuth2 / SSO]  --> Validate access token or session
    |
    v
Application Logic
```

## Memory Model

All modules use fixed-size arrays with compile-time configurable limits.
No dynamic allocation (`malloc`). Thread safety is not guaranteed - callers
must provide synchronization if needed.

## Dependencies

- **OpenSSL**: Used for HMAC-SHA256 in JWT signing/verification.
- **Standard C Library**: `<string.h>`, `<stdio.h>`, `<stdlib.h>`, `<time.h>`, `<math.h>`.

## Security Considerations

1. Auth codes are single-use and short-lived (10 min)
2. Access tokens limited to 1 hour TTL
3. Refresh tokens have 24-hour TTL
4. Tickets are consumed on first validation (replay protection)
5. RBAC cache is invalidated on any role change
6. Rate limiter entries auto-cleaned after 24h inactivity
7. SAML assertions include NotBefore/NotOnOrAfter bounds
