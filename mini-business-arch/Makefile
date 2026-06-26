CC       = gcc
CFLAGS   = -Wall -Wextra -O2 -I include
LDFLAGS  = -lm
OUTDIR   = bin
EXAMPLES = examples
TESTS    = tests

SRC      = src
INC      = include

# Example executables
EXE_DDD      = $(OUTDIR)/example_ddd
EXE_CQRS     = $(OUTDIR)/example_cqrs
EXE_SAGA     = $(OUTDIR)/example_saga
EXE_WORKFLOW = $(OUTDIR)/demo_workflow
EXE_BPMN     = $(OUTDIR)/demo_bpmn

# Test executables
TEST_DDD      = $(OUTDIR)/test_ddd
TEST_CQRS     = $(OUTDIR)/test_cqrs
TEST_SAGA     = $(OUTDIR)/test_saga
TEST_WORKFLOW = $(OUTDIR)/test_workflow
TEST_BPMN     = $(OUTDIR)/test_bpmn
TEST_PATTERNS = $(OUTDIR)/test_patterns

EXAMPLES_BIN = $(EXE_DDD) $(EXE_CQRS) $(EXE_SAGA) $(EXE_WORKFLOW) $(EXE_BPMN)
TESTS_BIN    = $(TEST_DDD) $(TEST_CQRS) $(TEST_SAGA) $(TEST_WORKFLOW) $(TEST_BPMN) $(TEST_PATTERNS)

.PHONY: all clean test

all: $(OUTDIR) $(EXAMPLES_BIN)

$(OUTDIR):
	mkdir -p $(OUTDIR)

# === Examples ===

$(EXE_DDD): $(EXAMPLES)/example_ddd.c $(SRC)/ddd_model.c $(INC)/ddd_model.h
	$(CC) $(CFLAGS) $< $(SRC)/ddd_model.c -o $@ $(LDFLAGS)

$(EXE_CQRS): $(EXAMPLES)/example_cqrs.c $(SRC)/cqrs_es.c $(SRC)/ddd_model.c $(INC)/cqrs_es.h $(INC)/ddd_model.h
	$(CC) $(CFLAGS) $< $(SRC)/cqrs_es.c $(SRC)/ddd_model.c -o $@ $(LDFLAGS)

$(EXE_SAGA): $(EXAMPLES)/example_saga.c $(SRC)/saga_orch.c $(SRC)/ddd_model.c $(INC)/saga_orch.h $(INC)/ddd_model.h
	$(CC) $(CFLAGS) $< $(SRC)/saga_orch.c $(SRC)/ddd_model.c -o $@ $(LDFLAGS)

$(EXE_WORKFLOW): $(EXAMPLES)/demo_workflow.c $(SRC)/workflow_engine.c $(INC)/workflow_engine.h $(INC)/ddd_model.h
	$(CC) $(CFLAGS) $< $(SRC)/workflow_engine.c -o $@ $(LDFLAGS)

$(EXE_BPMN): $(EXAMPLES)/demo_bpmn.c $(SRC)/bpmn_model.c $(INC)/bpmn_model.h
	$(CC) $(CFLAGS) $< $(SRC)/bpmn_model.c -o $@ $(LDFLAGS)

# === Tests ===

$(TEST_DDD): $(TESTS)/test_ddd.c $(SRC)/ddd_model.c $(INC)/ddd_model.h
	$(CC) $(CFLAGS) $< $(SRC)/ddd_model.c -o $@ $(LDFLAGS)

$(TEST_CQRS): $(TESTS)/test_cqrs.c $(SRC)/cqrs_es.c $(SRC)/ddd_model.c $(INC)/cqrs_es.h $(INC)/ddd_model.h
	$(CC) $(CFLAGS) $< $(SRC)/cqrs_es.c $(SRC)/ddd_model.c -o $@ $(LDFLAGS)

$(TEST_SAGA): $(TESTS)/test_saga.c $(SRC)/saga_orch.c $(SRC)/ddd_model.c $(INC)/saga_orch.h $(INC)/ddd_model.h
	$(CC) $(CFLAGS) $< $(SRC)/saga_orch.c $(SRC)/ddd_model.c -o $@ $(LDFLAGS)

$(TEST_WORKFLOW): $(TESTS)/test_workflow.c $(SRC)/workflow_engine.c $(INC)/workflow_engine.h $(INC)/ddd_model.h
	$(CC) $(CFLAGS) $< $(SRC)/workflow_engine.c -o $@ $(LDFLAGS)

$(TEST_BPMN): $(TESTS)/test_bpmn.c $(SRC)/bpmn_model.c $(INC)/bpmn_model.h
	$(CC) $(CFLAGS) $< $(SRC)/bpmn_model.c -o $@ $(LDFLAGS)

$(TEST_PATTERNS): $(TESTS)/test_patterns.c $(SRC)/business_patterns.c $(INC)/business_patterns.h
	$(CC) $(CFLAGS) $< $(SRC)/business_patterns.c -o $@ $(LDFLAGS)

test: $(OUTDIR) $(TESTS_BIN)
	@echo "=== Running Tests ==="
	@$(TEST_DDD)       || (echo "FAIL: test_ddd" && exit 1)
	@$(TEST_CQRS)      || (echo "FAIL: test_cqrs" && exit 1)
	@$(TEST_SAGA)      || (echo "FAIL: test_saga" && exit 1)
	@$(TEST_WORKFLOW)  || (echo "FAIL: test_workflow" && exit 1)
	@$(TEST_BPMN)      || (echo "FAIL: test_bpmn" && exit 1)
	@$(TEST_PATTERNS)  || (echo "FAIL: test_patterns" && exit 1)
	@echo ""
	@echo "=== All Tests Passed ==="

clean:
	rm -rf $(OUTDIR)
