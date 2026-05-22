CC       = gcc
CFLAGS   = -std=c99 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -O2 -g
INCDIR   = include
SRCDIR   = src
EXAMDIR  = examples
DEMODIR  = demos
BINDIR   = bin
INC      = -I$(INCDIR)

SRCS     = $(SRCDIR)/message_queue.c $(SRCDIR)/distributed_cache.c \
           $(SRCDIR)/config_center.c $(SRCDIR)/service_registry.c \
           $(SRCDIR)/gateway_routing.c
OBJS     = $(SRCS:.c=.o)
EXAMPLES = $(BINDIR)/example_broker $(BINDIR)/example_cache $(BINDIR)/example_config
DEMOS    = $(BINDIR)/demo_full_stack $(BINDIR)/demo_integration

.PHONY: all clean examples demos

all: $(OBJS)
	@echo "Objects built. Use 'make examples' or 'make demos'."

$(SRCDIR)/%.o: $(SRCDIR)/%.c $(INCDIR)/%.h
	$(CC) $(CFLAGS) $(INC) -c $< -o $@

$(BINDIR)/example_broker: $(EXAMDIR)/example_broker.c $(OBJS) | $(BINDIR)
	$(CC) $(CFLAGS) $(INC) $< $(OBJS) -o $@

$(BINDIR)/example_cache: $(EXAMDIR)/example_cache.c $(OBJS) | $(BINDIR)
	$(CC) $(CFLAGS) $(INC) $< $(OBJS) -o $@ -lm

$(BINDIR)/example_config: $(EXAMDIR)/example_config.c $(OBJS) | $(BINDIR)
	$(CC) $(CFLAGS) $(INC) $< $(OBJS) -o $@

$(BINDIR)/demo_full_stack: $(DEMODIR)/demo_full_stack.c $(OBJS) | $(BINDIR)
	$(CC) $(CFLAGS) $(INC) $< $(OBJS) -o $@

$(BINDIR)/demo_integration: $(DEMODIR)/demo_integration.c $(OBJS) | $(BINDIR)
	$(CC) $(CFLAGS) $(INC) $< $(OBJS) -o $@ -lm

$(BINDIR):
	mkdir -p $(BINDIR)

examples: $(EXAMPLES)

demos: $(DEMOS)

clean:
	rm -f $(SRCDIR)/*.o
	rm -f $(BINDIR)/*.exe
