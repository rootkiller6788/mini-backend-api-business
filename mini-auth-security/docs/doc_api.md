# mini-auth-security API Reference

## 1. OAuth2 Server (`oauth2_server.h`)

### Types

| Type | Description |
|------|-------------|
| `OAuth2GrantType` | Auth code, client credentials, refresh token |
| `OAuth2TokenType` | Bearer or MAC |
| `OAuth2Client` | Client registration with redirect URI and scope |
| `OAuth2AuthCode` | Temporary authorization code with expiry |
| `OAuth2TokenEntry` | Access + refresh token pair with expiry and revocation |
| `OAuth2Server` | Top-level server state container |

### Functions

| Function | Description |
|----------|-------------|
| `oauth2_server_init` | Initialize the OAuth2 server state |
| `oauth2_register_client` | Register a new OAuth2 client (confidential or public) |
| `oauth2_validate_client` | Validate client_id/secret pair |
| `oauth2_create_auth_code` | Create authorization code for auth code flow |
| `oauth2_exchange_code_for_token` | Exchange auth code for token pair |
| `oauth2_client_credentials_grant` | Direct token grant for machine-to-machine |
| `oauth2_refresh_token` | Issue new token pair from refresh token |
| `oauth2_validate_access_token` | Validate bearer access token |
| `oauth2_revoke_token` | Revoke an access token |
| `oauth2_generate_random_hex` | Generate random hex string (for tokens) |
| `oauth2_dump_state` | Debug dump of server state |

### Error Codes

| Return | Meaning |
|--------|---------|
| 0 | Success |
| -1 | Storage full / not found |
| -2 | Duplicate / invalid client |
| -3 | Already used / expired |
| -4 | Expired |
| -5 | Client mismatch |
| -6 | Redirect URI mismatch |

---

## 2. JWT Auth (`jwt_auth.h`)

### Types

| Type | Description |
|------|-------------|
| `JwtAlgorithm` | HS256/384/512, RS256/384/512 |
| `JwtHeader` | alg, typ, kid fields |
| `JwtClaims` | Standard claims: iss, sub, aud, exp, iat, nbf, jti, scope |
| `JwtSigningKey` | Key material with algorithm |
| `JwtEngine` | Encoder/decoder with signing callback |

### Functions

| Function | Description |
|----------|-------------|
| `jwt_engine_init` | Initialize with algorithm and key |
| `jwt_encode` | Create signed JWT from claims |
| `jwt_decode` | Decode and verify JWT signature |
| `jwt_validate_expiry` | Check exp and nbf claims |
| `jwt_validate_issuer` | Verify iss claim |
| `jwt_validate_audience` | Verify aud claim |
| `jwt_validate_all` | Full validation in one call |
| `jwt_refresh_token` | Decode, update, re-encode |
| `jwt_claims_set_defaults` | Set iat/exp/nbf to sensible defaults |
| `jwt_base64url_encode` | Base64URL encoding (no padding) |
| `jwt_base64url_decode` | Base64URL decoding |
| `jwt_hs256_sign/verify` | HMAC-SHA256 operations |
| `jwt_rs256_sign_sim/verify_sim` | Simulated RSA-SHA256 |

### Token Format

```
base64url(header).base64url(payload).base64url(signature)
```

Header: `{"alg":"HS256","typ":"JWT"}`
Payload: `{"iss":"...","sub":"...","exp":...,...}`

---

## 3. RBAC Engine (`rbac_engine.h`)

### Types

| Type | Description |
|------|-------------|
| `RbacRoleId` | Enum: NONE, ADMIN, MODERATOR, EDITOR, VIEWER, CUSTOM |
| `RbacPermission` | Named permission with resource/action/mask |
| `RbacRole` | Named role with permission mask and parent hierarchy |
| `RbacUser` | User with assigned roles and cached permissions |
| `RbacEngine` | Full RBAC state |

### Predefined Permissions

| Constant | Value | Resource | Action |
|----------|-------|----------|--------|
| `PERM_READ_USERS` | `1<<0` | users | READ |
| `PERM_WRITE_USERS` | `1<<1` | users | WRITE |
| `PERM_DELETE_USERS` | `1<<2` | users | DELETE |
| `PERM_READ_POSTS` | `1<<3` | posts | READ |
| `PERM_WRITE_POSTS` | `1<<4` | posts | WRITE |
| `PERM_DELETE_POSTS` | `1<<5` | posts | DELETE |
| `PERM_READ_COMMENTS` | `1<<6` | comments | READ |
| `PERM_WRITE_COMMENTS` | `1<<7` | comments | WRITE |
| `PERM_DELETE_COMMENTS` | `1<<8` | comments | DELETE |
| `PERM_MANAGE_ROLES` | `1<<9` | roles | MANAGE |
| `PERM_MANAGE_SYSTEM` | `1<<10` | system | MANAGE |
| `PERM_READ_METRICS` | `1<<11` | metrics | READ |
| `PERM_MANAGE_USERS` | `1<<12` | users | MANAGE |

### Key Functions

| Function | Description |
|----------|-------------|
| `rbac_setup_default_roles` | Create ADMIN/EDITOR/MODERATOR/VIEWER with hierarchy |
| `rbac_check_permission` | Single permission bitmask check |
| `rbac_check_permission_multi` | Check multiple permissions (ALL or ANY mode) |
| `rbac_get_effective_permissions` | Resolve all permissions including inherited |
| `rbac_dump_user` | Debug print user's roles and permissions |

### Default Role Hierarchy

```
ADMIN (priority 10)
 └── MODERATOR (priority 3)
      └── EDITOR (priority 2)
           └── VIEWER (priority 1)
```

---

## 4. SSO Model (`sso_model.h`)

### Types

| Type | Description |
|------|-------------|
| `SsoProtocol` | SAML2, OIDC, CAS |
| `SsoBinding` | Redirect, POST, Artifact |
| `SsoServiceProvider` | SP metadata with ACS/SLO URLs |
| `SsoIdentityProvider` | IDP metadata with signing keys |
| `SsoTicket` | One-time ticket for token exchange |
| `SsoSession` | User session with federation |
| `SsoModel` | Full SSO state |

### Functions

| Function | Description |
|----------|-------------|
| `sso_model_init` | Initialize SSO model |
| `sso_idp_configure` | Set IDP metadata |
| `sso_register_sp` | Register a service provider |
| `sso_establish_trust` | Mark SP as trusted |
| `sso_create_user` | Add user to directory |
| `sso_authenticate_user` | Authenticate and create session + SAML assertion |
| `sso_create_ticket` | Create one-time ticket |
| `sso_validate_ticket` | Validate and consume ticket, return user + assertion |
| `sso_validate_session` | Check session validity |
| `sso_logout` | Invalidate session |
| `sso_generate_saml_assertion` | Generate SAML 2.0 XML assertion |

---

## 5. Rate Limiter (`rate_limiter.h`)

### Types

| Type | Description |
|------|-------------|
| `RateAlgo` | Fixed window, sliding window, token bucket |
| `RateScope` | User, IP, API, Global |
| `RateLimitEntry` | Single rate limit state with key |
| `RateLimitResult` | Allow/deny with remaining count and reset time |
| `RateLimiter` | Full limiter state |

### Algorithms

| Algorithm | Description |
|-----------|-------------|
| Fixed Window | Counter resets at window boundary |
| Sliding Window | Timestamp ring buffer, smooth decay |
| Token Bucket | Continuous refill at specified rate |

### Functions

| Function | Description |
|----------|-------------|
| `rate_limiter_init` | Initialize with default limit/window |
| `rate_limiter_check` | Single check with auto-entry creation |
| `rate_limiter_check_multi` | Batch check, returns only if ALL pass |
| `rate_generate_headers` | Generate X-RateLimit-* HTTP headers |
| `rate_redis_sim_acquire` | Simulated Redis atomic counter |
| `rate_lua_sim_check` | Simulated Redis+Lua script |
| `rate_limiter_cleanup` | Remove entries older than 24h |

### Headers

| Header | Format |
|--------|--------|
| `X-RateLimit-Limit` | Max requests |
| `X-RateLimit-Remaining` | Remaining in window |
| `X-RateLimit-Reset` | Seconds until reset |
