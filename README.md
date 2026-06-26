# Mini Backend API Business

A collection of **from-scratch, zero-dependency C implementations** of backend services, API engineering, authentication, business architecture, and infrastructure patterns. Each module models real backend system behavior — from HTTP servers and REST/GraphQL APIs to OAuth2 authentication, DDD/CQRS business architecture, message queues, and job scheduling systems.

## Modules

| Module | Topics | Key References |
|--------|--------|----------------|
| [mini-web-server](mini-web-server/) | HTTP/1.1 server, middleware chain, routing (trie/radix), static file serving, CGI/FastCGI sim | NGINX internals, Express.js |
| [mini-api-engineering](mini-api-engineering/) | RESTful API design, GraphQL schema/query parser, gRPC sim, OpenAPI/Swagger spec builder, versioning | REST, GraphQL Spec, gRPC |
| [mini-backend-framework](mini-backend-framework/) | DI/IoC container, ORM (ActiveRecord/DataMapper), MVC pattern, input validation, serialization | Spring, Django, Rails |
| [mini-auth-security](mini-auth-security/) | OAuth2 (auth code, client creds), JWT sign/verify, RBAC/ABAC, SSO model, rate limiting | OAuth2 RFC 6749, JWT RFC 7519 |
| [mini-business-arch](mini-business-arch/) | DDD (entity/value object/aggregate/repository), CQRS command/query separation, event sourcing, saga pattern | Eric Evans DDD, Martin Fowler |
| [mini-business-infra](mini-business-infra/) | Message queue (pub/sub, dead letter), distributed cache (LRU, TTL), config center (hot reload), service registration | RabbitMQ, Redis, Nacos |
| [mini-job-system](mini-job-system/) | Cron scheduler, delayed job queue, work queue (worker pool), task retry with backoff, job progress tracking | Celery, Sidekiq, Quartz |

## Design Philosophy

- **Zero external dependencies** — pure C (C99/C11), only `libc` and `libm`
- **Self-contained modules** — each directory has its own `Makefile`, `include/`, `src/`, `examples/`, `demos/`, `tests/`
- **Backend simulation in user-space** — educational models of backend services, auth flows, and business architecture
- **Theory-to-code mapping** — every module includes `docs/` with reference-alignment notes
- **Practical demos** — HTTP server, OAuth2 auth server, message queue engine, job scheduler, and more

## Building

Each module is standalone. Navigate to a module directory and run:

```bash
cd mini-web-server
make all    # build everything
make test   # run tests
```

Requires **GCC** and **GNU Make**.

## Project Structure

```
mini-backend-api-business/
├── mini-web-server/             # Web Server
├── mini-api-engineering/        # API Engineering
├── mini-backend-framework/      # Backend Framework
├── mini-auth-security/          # Authentication & Security
├── mini-business-arch/          # Business Architecture (DDD/CQRS)
├── mini-business-infra/         # Business Infrastructure
└── mini-job-system/             # Job Scheduling System
```

## License

MIT
