CC       = gcc
CFLAGS   = -Wall -Wextra -O2 -I include
LDFLAGS  = -lm -Wl,--stack,33554432
OUTDIR   = bin
EXE      = .exe

SRC_REST     = src/rest_design.c
SRC_GRAPHQL  = src/graphql_engine.c
SRC_GRPC     = src/grpc_sim.c
SRC_OPENAPI  = src/openapi_builder.c
SRC_VERSION  = src/api_version.c

DEMO1 = $(OUTDIR)/demo_rest_crud$(EXE)
DEMO2 = $(OUTDIR)/demo_graphql_query$(EXE)
DEMO3 = $(OUTDIR)/demo_openapi_spec$(EXE)
TEST  = $(OUTDIR)/test_all$(EXE)

.PHONY: all test clean

all: $(DEMO1) $(DEMO2) $(DEMO3) $(TEST)

$(DEMO1): examples/demo_rest_crud.c $(SRC_REST) | $(OUTDIR)
	$(CC) $(CFLAGS) -o $@ examples/demo_rest_crud.c $(SRC_REST) $(LDFLAGS)

$(DEMO2): examples/demo_graphql_query.c $(SRC_GRAPHQL) | $(OUTDIR)
	$(CC) $(CFLAGS) -o $@ examples/demo_graphql_query.c $(SRC_GRAPHQL) $(LDFLAGS)

$(DEMO3): examples/demo_openapi_spec.c $(SRC_OPENAPI) | $(OUTDIR)
	$(CC) $(CFLAGS) -o $@ examples/demo_openapi_spec.c $(SRC_OPENAPI) $(LDFLAGS)

$(TEST): tests/test_all.c $(SRC_REST) $(SRC_GRAPHQL) $(SRC_GRPC) $(SRC_OPENAPI) $(SRC_VERSION) | $(OUTDIR)
	$(CC) $(CFLAGS) -o $@ tests/test_all.c $(SRC_REST) $(SRC_GRAPHQL) $(SRC_GRPC) $(SRC_OPENAPI) $(SRC_VERSION) $(LDFLAGS)

$(OUTDIR):
	mkdir -p $(OUTDIR)

test: all
	@echo "=== Running demo_rest_crud ==="
	$(DEMO1)
	@echo ""
	@echo "=== Running demo_graphql_query ==="
	$(DEMO2)
	@echo ""
	@echo "=== Running demo_openapi_spec ==="
	$(DEMO3)
	@echo ""
	@echo "=== Running unit tests ==="
	$(TEST)
	@echo ""
	@echo "=== All tests passed ==="

clean:
	rm -rf $(OUTDIR)
