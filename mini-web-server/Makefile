CC       := gcc
CFLAGS   := -Wall -Wextra -O2 -I include
LDFLAGS  := -lm -lpthread -lws2_32
OUTDIR   := bin
SRCDIR   := src
EXAMDIR  := examples
TESTDIR  := tests

SRCS     := $(wildcard $(SRCDIR)/*.c)
OBJS     := $(patsubst $(SRCDIR)/%.c,$(OUTDIR)/%.o,$(SRCS))
EXAMPLES := http_parse_demo middleware_chain_demo static_server_demo json_api_demo websocket_demo
EXAM_BIN := $(addprefix $(OUTDIR)/,$(EXAMPLES))
TEST_BIN := $(OUTDIR)/test_all

.PHONY: all clean run test

all: $(OUTDIR) $(OBJS) $(EXAM_BIN)
	@echo "=== Build complete ==="

test: $(OUTDIR) $(OBJS) $(TEST_BIN)
	@echo "=== Running tests ==="
	$(TEST_BIN)

$(OUTDIR):
	@mkdir -p "$(OUTDIR)"

$(OUTDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OUTDIR)/http_parse_demo: $(EXAMDIR)/http_parse_demo.c $(OUTDIR)/http_core.o
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

$(OUTDIR)/middleware_chain_demo: $(EXAMDIR)/middleware_chain_demo.c $(OUTDIR)/http_core.o $(OUTDIR)/middleware.o
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

$(OUTDIR)/static_server_demo: $(EXAMDIR)/static_server_demo.c $(OUTDIR)/http_core.o $(OUTDIR)/static_serve.o
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

$(OUTDIR)/json_api_demo: $(EXAMDIR)/json_api_demo.c $(OUTDIR)/http_core.o $(OUTDIR)/json_helper.o $(OUTDIR)/router.o $(OUTDIR)/middleware.o
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

$(OUTDIR)/websocket_demo: $(EXAMDIR)/websocket_demo.c $(OUTDIR)/http_core.o $(OUTDIR)/websocket.o
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

$(OUTDIR)/test_all: $(TESTDIR)/test_all.c $(OBJS)
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

run: $(OUTDIR)/http_parse_demo
	$(OUTDIR)/http_parse_demo

clean:
	@rm -rf "$(OUTDIR)"
