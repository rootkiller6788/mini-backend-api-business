# mini-job-system Makefile
# C99 Job Scheduling System

CC      = gcc
CFLAGS  = -std=c99 -Wall -Wextra -O2 -Iinclude
LDFLAGS =

ifeq ($(OS),Windows_NT)
    LDFLAGS += -lwinmm
else
    LDFLAGS += -lpthread -lrt
endif

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

.PHONY: all clean examples demos lib
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
	rm -f $(EXAMPLES) $(DEMOS)
	rm -f $(EXAMPLES:%=%.exe) $(DEMOS:%=%.exe)

help:
	@echo "Targets:"
	@echo "  all                  - Build library, examples, and demos"
	@echo "  lib                  - Build static library libminijob.a"
	@echo "  examples             - Build all example programs"
	@echo "  demos                - Build all demo programs"
	@echo "  clean                - Remove build artifacts"
	@echo "  run-example-basic    - Run basic example"
	@echo "  run-example-cron     - Run cron example"
	@echo "  run-example-retry    - Run retry example"
	@echo "  run-demo-system      - Run system demo"
	@echo "  run-demo-advanced    - Run advanced demo"
