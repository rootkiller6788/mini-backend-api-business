CC       = gcc
CFLAGS   = -Wall -Wextra -O2 -I include
LDFLAGS  = -lm
OUTDIR   = bin
EXAMPLES = examples

SRC      = src
INC      = include

EXE_DDD  = $(OUTDIR)\example_ddd
EXE_CQRS = $(OUTDIR)\example_cqrs
EXE_SAGA = $(OUTDIR)\example_saga

BINARIES = $(EXE_DDD) $(EXE_CQRS) $(EXE_SAGA)

.PHONY: all clean test

all: $(OUTDIR) $(BINARIES)

$(OUTDIR):
	if not exist "$(OUTDIR)" mkdir "$(OUTDIR)"

$(EXE_DDD): $(EXAMPLES)\example_ddd.c $(SRC)\ddd_model.c $(INC)\ddd_model.h
	$(CC) $(CFLAGS) $< $(SRC)\ddd_model.c -o $@ $(LDFLAGS)

$(EXE_CQRS): $(EXAMPLES)\example_cqrs.c $(SRC)\cqrs_es.c $(SRC)\ddd_model.c $(INC)\cqrs_es.h $(INC)\ddd_model.h
	$(CC) $(CFLAGS) $< $(SRC)\cqrs_es.c $(SRC)\ddd_model.c -o $@ $(LDFLAGS)

$(EXE_SAGA): $(EXAMPLES)\example_saga.c $(SRC)\saga_orch.c $(SRC)\ddd_model.c $(INC)\saga_orch.h $(INC)\ddd_model.h
	$(CC) $(CFLAGS) $< $(SRC)\saga_orch.c $(SRC)\ddd_model.c -o $@ $(LDFLAGS)

test: all
	$(EXE_DDD)
	$(EXE_CQRS)
	$(EXE_SAGA)

clean:
	if exist "$(OUTDIR)" rmdir /s /q "$(OUTDIR)"
