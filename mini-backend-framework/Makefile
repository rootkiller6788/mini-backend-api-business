CC       = gcc
CFLAGS   = -Wall -Wextra -O2 -std=c99 -I include
LDFLAGS  = -lm
SRCDIR   = src
EXDIR    = examples
TESTDIR  = tests
OUTDIR   = bin

SRCS     = $(SRCDIR)/di_container.c $(SRCDIR)/orm_core.c \
           $(SRCDIR)/mvc_pattern.c $(SRCDIR)/validator.c \
           $(SRCDIR)/serializer.c $(SRCDIR)/middleware.c \
           $(SRCDIR)/rate_limiter.c $(SRCDIR)/cache.c \
           $(SRCDIR)/pool.c $(SRCDIR)/config.c

OBJS     = $(SRCS:.c=.o)

DEMOS    = di_demo orm_demo mvc_demo

.PHONY: all test clean test_only

all: $(DEMOS)

$(OUTDIR):
	mkdir -p $(OUTDIR)

di_demo: $(SRCDIR)/di_container.o $(EXDIR)/di_demo.c | $(OUTDIR)
	$(CC) $(CFLAGS) $(SRCDIR)/di_container.o $(EXDIR)/di_demo.c -o $(OUTDIR)/di_demo $(LDFLAGS)

orm_demo: $(SRCDIR)/orm_core.o $(EXDIR)/orm_demo.c | $(OUTDIR)
	$(CC) $(CFLAGS) $(SRCDIR)/orm_core.o $(EXDIR)/orm_demo.c -o $(OUTDIR)/orm_demo $(LDFLAGS)

mvc_demo: $(SRCDIR)/mvc_pattern.o $(SRCDIR)/validator.o $(SRCDIR)/serializer.o $(EXDIR)/mvc_demo.c | $(OUTDIR)
	$(CC) $(CFLAGS) $(SRCDIR)/mvc_pattern.o $(SRCDIR)/validator.o $(SRCDIR)/serializer.o $(EXDIR)/mvc_demo.c -o $(OUTDIR)/mvc_demo $(LDFLAGS)

# --- Object compilation rules ---

$(SRCDIR)/di_container.o: $(SRCDIR)/di_container.c include/di_container.h
	$(CC) $(CFLAGS) -c $(SRCDIR)/di_container.c -o $(SRCDIR)/di_container.o

$(SRCDIR)/orm_core.o: $(SRCDIR)/orm_core.c include/orm_core.h
	$(CC) $(CFLAGS) -c $(SRCDIR)/orm_core.c -o $(SRCDIR)/orm_core.o

$(SRCDIR)/mvc_pattern.o: $(SRCDIR)/mvc_pattern.c include/mvc_pattern.h
	$(CC) $(CFLAGS) -c $(SRCDIR)/mvc_pattern.c -o $(SRCDIR)/mvc_pattern.o

$(SRCDIR)/validator.o: $(SRCDIR)/validator.c include/validator.h
	$(CC) $(CFLAGS) -c $(SRCDIR)/validator.c -o $(SRCDIR)/validator.o

$(SRCDIR)/serializer.o: $(SRCDIR)/serializer.c include/serializer.h
	$(CC) $(CFLAGS) -c $(SRCDIR)/serializer.c -o $(SRCDIR)/serializer.o

$(SRCDIR)/middleware.o: $(SRCDIR)/middleware.c include/middleware.h
	$(CC) $(CFLAGS) -c $(SRCDIR)/middleware.c -o $(SRCDIR)/middleware.o

$(SRCDIR)/rate_limiter.o: $(SRCDIR)/rate_limiter.c include/rate_limiter.h
	$(CC) $(CFLAGS) -c $(SRCDIR)/rate_limiter.c -o $(SRCDIR)/rate_limiter.o

$(SRCDIR)/cache.o: $(SRCDIR)/cache.c include/cache.h
	$(CC) $(CFLAGS) -c $(SRCDIR)/cache.c -o $(SRCDIR)/cache.o

$(SRCDIR)/pool.o: $(SRCDIR)/pool.c include/pool.h
	$(CC) $(CFLAGS) -c $(SRCDIR)/pool.c -o $(SRCDIR)/pool.o

$(SRCDIR)/config.o: $(SRCDIR)/config.c include/config.h
	$(CC) $(CFLAGS) -c $(SRCDIR)/config.c -o $(SRCDIR)/config.o

# --- Test target ---

test: $(OBJS) $(TESTDIR)/test_all.c $(DEMOS) | $(OUTDIR)
	$(CC) $(CFLAGS) $(OBJS) $(TESTDIR)/test_all.c -o $(OUTDIR)/test_all $(LDFLAGS)
	@echo ""
	@echo "Running comprehensive test suite..."
	@echo ""
	$(OUTDIR)/test_all
	@echo ""
	@echo "Running demos..."
	@echo ""
	$(OUTDIR)/di_demo
	@echo ""
	$(OUTDIR)/orm_demo
	@echo ""
	$(OUTDIR)/mvc_demo
	@echo ""
	@echo "All tests and demos completed."

# --- Run only tests (no demos) ---

test_only: $(OBJS) $(TESTDIR)/test_all.c | $(OUTDIR)
	$(CC) $(CFLAGS) $(OBJS) $(TESTDIR)/test_all.c -o $(OUTDIR)/test_all $(LDFLAGS)
	$(OUTDIR)/test_all

clean:
	rm -rf $(OUTDIR)
	rm -f $(SRCDIR)/*.o
