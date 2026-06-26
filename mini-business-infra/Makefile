CC       = gcc
CFLAGS   = -std=c99 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -O2 -g
INCDIR   = include
SRCDIR   = src
TESTDIR  = tests
EXAMDIR  = examples
DEMODIR  = demos
BINDIR   = bin
INC      = -I$(INCDIR)

SRCS     = $(SRCDIR)/message_queue.c $(SRCDIR)/distributed_cache.c \
           $(SRCDIR)/config_center.c $(SRCDIR)/service_registry.c \
           $(SRCDIR)/gateway_routing.c $(SRCDIR)/circuit_breaker.c \
           $(SRCDIR)/consistent_hash.c $(SRCDIR)/bloom_filter.c
OBJS     = $(SRCS:.c=.o)
EXAMPLES = $(BINDIR)/example_broker $(BINDIR)/example_cache $(BINDIR)/example_config
DEMOS    = $(BINDIR)/demo_full_stack $(BINDIR)/demo_integration

.PHONY: all test clean examples demos

all: $(OBJS)
	@echo "Objects built. Use 'make examples', 'make demos', or 'make test'."

$(SRCDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) $(INC) -c $< -o $@

$(BINDIR)/example_broker: $(EXAMDIR)/example_broker.c $(OBJS) | $(BINDIR)
	$(CC) $(CFLAGS) $(INC) $< $(OBJS) -o $@

$(BINDIR)/example_cache: $(EXAMDIR)/example_cache.c $(OBJS) | $(BINDIR)
	$(CC) $(CFLAGS) $(INC) $< $(OBJS) -o $@ -lm

$(BINDIR)/example_config: $(EXAMDIR)/example_config.c $(OBJS) | $(BINDIR)
	$(CC) $(CFLAGS) $(INC) $< $(OBJS) -o $@

$(BINDIR)/demo_full_stack: $(DEMODIR)/demo_full_stack.c $(OBJS) | $(BINDIR)
	$(CC) $(CFLAGS) $(INC) $< $(OBJS) -o $@ -lm

$(BINDIR)/demo_integration: $(DEMODIR)/demo_integration.c $(OBJS) | $(BINDIR)
	$(CC) $(CFLAGS) $(INC) $< $(OBJS) -o $@ -lm

$(BINDIR)/test_runner.exe: $(TESTDIR)/test_runner.c $(OBJS) | $(BINDIR)
	$(CC) $(CFLAGS) $(INC) $< $(OBJS) -o $@ -lm

$(BINDIR):
	mkdir -p $(BINDIR)

test: $(BINDIR)/test_runner.exe
	@echo "Running tests..."
	./$(BINDIR)/test_runner.exe

examples: $(EXAMPLES)

demos: $(DEMOS)

clean:
	rm -f $(SRCDIR)/*.o
	rm -f $(BINDIR)/*.exe
	rm -f $(BINDIR)/test_runner.exe
	rm -f tests/*.tmp
