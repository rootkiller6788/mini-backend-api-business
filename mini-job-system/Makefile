# mini-job-system Makefile
# C99 Job Scheduling System

CC      = gcc
CFLAGS  = -std=c99 -Wall -Wextra -O2 -Iinclude
LDFLAGS =

LDFLAGS += -lpthread

SRC_DIR   = src
INC_DIR   = include
EXAM_DIR  = examples
DEMO_DIR  = demos
BUILD_DIR = build

SRCS  = $(wildcard $(SRC_DIR)/*.c)
OBJS  = $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

EXAMPLES = $(EXAM_DIR)/example_basic \
           $(EXAM_DIR)/example_cron \
           $(EXAM_DIR)/example_retry

DEMOS    = $(DEMO_DIR)/demo_system \
           $(DEMO_DIR)/demo_advanced

LIBS     = $(BUILD_DIR)/libminijob.a

.PHONY: all clean examples demos lib test
.PHONY: run-example-basic run-example-cron run-example-retry
.PHONY: run-demo-system run-demo-advanced

all: lib examples demos

$(BUILD_DIR):
	mkdir $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(LIBS): $(OBJS)
	$(AR) rcs $@ $^

lib: $(LIBS)

examples: $(EXAMPLES)

demos: $(DEMOS)

$(EXAM_DIR)/example_basic: $(EXAM_DIR)/example_basic.c $(LIBS)
	$(CC) $(CFLAGS) $< -L$(BUILD_DIR) -lminijob $(LDFLAGS) -o $@

$(EXAM_DIR)/example_cron: $(EXAM_DIR)/example_cron.c $(LIBS)
	$(CC) $(CFLAGS) $< -L$(BUILD_DIR) -lminijob $(LDFLAGS) -o $@

$(EXAM_DIR)/example_retry: $(EXAM_DIR)/example_retry.c $(LIBS)
	$(CC) $(CFLAGS) $< -L$(BUILD_DIR) -lminijob $(LDFLAGS) -o $@

$(DEMO_DIR)/demo_system: $(DEMO_DIR)/demo_system.c $(LIBS)
	$(CC) $(CFLAGS) $< -L$(BUILD_DIR) -lminijob $(LDFLAGS) -o $@

$(DEMO_DIR)/demo_advanced: $(DEMO_DIR)/demo_advanced.c $(LIBS)
	$(CC) $(CFLAGS) $< -L$(BUILD_DIR) -lminijob $(LDFLAGS) -o $@

run-example-basic: $(EXAM_DIR)/example_basic
	./$(EXAM_DIR)/example_basic

run-example-cron: $(EXAM_DIR)/example_cron
	./$(EXAM_DIR)/example_cron

run-example-retry: $(EXAM_DIR)/example_retry
	./$(EXAM_DIR)/example_retry

run-demo-system: $(DEMO_DIR)/demo_system
	./$(DEMO_DIR)/demo_system

run-demo-advanced: $(DEMO_DIR)/demo_advanced
	./$(DEMO_DIR)/demo_advanced

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(EXAMPLES) $(DEMOS) $(TEST_BINS)
	rm -f $(EXAMPLES:%=%.exe) $(DEMOS:%=%.exe)

# === Test targets ===

TEST_DIR   = tests
TEST_SRCS  = $(wildcard $(TEST_DIR)/test_*.c)
TEST_BINS  = $(TEST_SRCS:$(TEST_DIR)/%.c=$(BUILD_DIR)/%)

$(BUILD_DIR)/test_%: $(TEST_DIR)/test_%.c $(LIBS)
	$(CC) $(CFLAGS) $< -L$(BUILD_DIR) -lminijob $(LDFLAGS) -o $@

test: $(TEST_BINS)
	@echo "========================================="
	@echo "  Running mini-job-system tests"
	@echo "========================================="
	@FAILED=0; \
	for t in $(sort $(TEST_BINS)); do \
		./$$t; \
		if [ $$? -ne 0 ]; then FAILED=$$((FAILED+1)); fi; \
	done; \
	echo "========================================="; \
	echo "  Test suites: $$(echo $(TEST_BINS) | wc -w)"; \
	if [ $$FAILED -eq 0 ]; then \
		echo "  ALL TESTS PASSED"; \
	else \
		echo "  FAILED: $$FAILED suite(s)"; \
		exit 1; \
	fi

help:
	@echo "Targets:"
	@echo "  all                  - Build library, examples, and demos"
	@echo "  lib                  - Build static library libminijob.a"
	@echo "  examples             - Build all example programs"
	@echo "  demos                - Build all demo programs"
	@echo "  test                 - Build and run all tests"
	@echo "  clean                - Remove build artifacts"
	@echo "  run-example-basic    - Run basic example"
	@echo "  run-example-cron     - Run cron example"
	@echo "  run-example-retry    - Run retry example"
	@echo "  run-demo-system      - Run system demo"
	@echo "  run-demo-advanced    - Run advanced demo"
