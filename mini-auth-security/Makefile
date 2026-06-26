CC       = gcc
CFLAGS   = -std=c99 -Wall -Wextra -O2
INCLUDES = -Iinclude
LDFLAGS  = -lssl -lcrypto -lm

SRCDIR   = src
INCDIR   = include
EXADIR   = examples
DEMDIR   = demos
OUTDIR   = build

SRCS = $(SRCDIR)/oauth2_server.c \
       $(SRCDIR)/jwt_auth.c \
       $(SRCDIR)/rbac_engine.c \
       $(SRCDIR)/sso_model.c \
       $(SRCDIR)/rate_limiter.c \
       $(SRCDIR)/crypto_utils.c \
       $(SRCDIR)/audit_log.c \
       $(SRCDIR)/session_manager.c \
       $(SRCDIR)/web_security.c

OBJS = $(SRCS:$(SRCDIR)/%.c=$(OUTDIR)/%.o)

EXAMPLES = example_oauth2 example_jwt example_rbac
DEMOS    = demo_sso demo_rate_limiter

all: dirs lib examples demos

test: dirs lib
	$(CC) $(CFLAGS) $(INCLUDES) tests/test_all.c $(OBJS) $(LDFLAGS) -Wl,--stack,33554432 -o $(OUTDIR)/test_all
	@echo "Running tests..."
	@PATH="$(OUTDIR):$$PATH" $(OUTDIR)/test_all && echo "=== All tests passed ==="

dirs:
	@mkdir -p "$(OUTDIR)"

lib: $(OBJS)
	ar rcs $(OUTDIR)/libminiauth.a $(OBJS)

$(OUTDIR)/%.o: $(SRCDIR)/%.c $(INCDIR)/%.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

examples: $(EXAMPLES)
demos: $(DEMOS)

example_oauth2: $(EXADIR)/example_oauth2.c $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) $< $(OBJS) $(LDFLAGS) -o $(OUTDIR)/$@

example_jwt: $(EXADIR)/example_jwt.c $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) $< $(OBJS) $(LDFLAGS) -o $(OUTDIR)/$@

example_rbac: $(EXADIR)/example_rbac.c $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) $< $(OBJS) $(LDFLAGS) -o $(OUTDIR)/$@

demo_sso: $(DEMDIR)/demo_sso.c $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) $< $(OBJS) $(LDFLAGS) -o $(OUTDIR)/$@

demo_rate_limiter: $(DEMDIR)/demo_rate_limiter.c $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) $< $(OBJS) $(LDFLAGS) -o $(OUTDIR)/$@

run_examples: examples
	@echo === Running Example OAuth2 ===
	$(OUTDIR)/example_oauth2
	@echo .
	@echo === Running Example JWT ===
	$(OUTDIR)/example_jwt
	@echo .
	@echo === Running Example RBAC ===
	$(OUTDIR)/example_rbac

run_demos: demos
	@echo === Running Demo SSO ===
	$(OUTDIR)/demo_sso
	@echo .
	@echo === Running Demo Rate Limiter ===
	$(OUTDIR)/demo_rate_limiter

run_all: run_examples run_demos

clean:
	@rm -rf "$(OUTDIR)"/* 2>/dev/null || true

.PHONY: all dirs lib examples demos test run_examples run_demos run_all clean
