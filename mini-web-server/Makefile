CC       := gcc
CFLAGS   := -Wall -Wextra -O2 -I include
LDFLAGS  := -lm
OUTDIR   := bin
SRCDIR   := src
EXAMDIR  := examples

SRCS     := $(wildcard $(SRCDIR)/*.c)
OBJS     := $(patsubst $(SRCDIR)/%.c,$(OUTDIR)/%.o,$(SRCS))
EXAMPLES := http_parse_demo middleware_chain_demo static_server_demo
EXAM_BIN := $(addprefix $(OUTDIR)/,$(EXAMPLES))

.PHONY: all clean run

all: $(OUTDIR) $(OBJS) $(EXAM_BIN)
	@echo "=== Build complete ==="

$(OUTDIR):
	@if not exist "$(OUTDIR)" mkdir "$(OUTDIR)"

$(OUTDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OUTDIR)/http_parse_demo: $(EXAMDIR)/http_parse_demo.c $(OUTDIR)/http_core.o
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

$(OUTDIR)/middleware_chain_demo: $(EXAMDIR)/middleware_chain_demo.c $(OUTDIR)/http_core.o $(OUTDIR)/middleware.o
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

$(OUTDIR)/static_server_demo: $(EXAMDIR)/static_server_demo.c $(OUTDIR)/http_core.o $(OUTDIR)/static_serve.o
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

run: $(OUTDIR)/http_parse_demo
	$(OUTDIR)/http_parse_demo.exe

clean:
	@if exist "$(OUTDIR)" rmdir /s /q "$(OUTDIR)"
