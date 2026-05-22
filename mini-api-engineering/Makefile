CC       = gcc
CFLAGS   = -Wall -Wextra -O2 -I include
LDFLAGS  = -lm
OUTDIR   = bin

SRC_REST     = src/rest_design.c
SRC_GRAPHQL  = src/graphql_engine.c
SRC_GRPC     = src/grpc_sim.c
SRC_OPENAPI  = src/openapi_builder.c
SRC_VERSION  = src/api_version.c

.PHONY: all test clean

all: $(OUTDIR)/demo_rest_crud $(OUTDIR)/demo_graphql_query $(OUTDIR)/demo_openapi_spec

$(OUTDIR)/demo_rest_crud: examples/demo_rest_crud.c $(SRC_REST) | $(OUTDIR)
	$(CC) $(CFLAGS) -o $@ examples/demo_rest_crud.c $(SRC_REST) $(LDFLAGS)

$(OUTDIR)/demo_graphql_query: examples/demo_graphql_query.c $(SRC_GRAPHQL) | $(OUTDIR)
	$(CC) $(CFLAGS) -o $@ examples/demo_graphql_query.c $(SRC_GRAPHQL) $(LDFLAGS)

$(OUTDIR)/demo_openapi_spec: examples/demo_openapi_spec.c $(SRC_OPENAPI) | $(OUTDIR)
	$(CC) $(CFLAGS) -o $@ examples/demo_openapi_spec.c $(SRC_OPENAPI) $(LDFLAGS)

$(OUTDIR):
	@if not exist "$(OUTDIR)" mkdir "$(OUTDIR)"

test: all
	@echo === Running demo_rest_crud ===
	$(OUTDIR)\demo_rest_crud
	@echo.
	@echo === Running demo_graphql_query ===
	$(OUTDIR)\demo_graphql_query
	@echo.
	@echo === Running demo_openapi_spec ===
	$(OUTDIR)\demo_openapi_spec
	@echo.
	@echo === All demos passed ===

clean:
	@if exist "$(OUTDIR)" rmdir /s /q "$(OUTDIR)"
