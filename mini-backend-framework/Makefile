CC       = gcc
CFLAGS   = -Wall -Wextra -O2 -std=c99 -I include
LDFLAGS  = -lm
SRCDIR   = src
EXDIR    = examples
OUTDIR   = bin

SRCS     = $(SRCDIR)/di_container.c $(SRCDIR)/orm_core.c \
           $(SRCDIR)/mvc_pattern.c $(SRCDIR)/validator.c \
           $(SRCDIR)/serializer.c

OBJS     = $(SRCS:.c=.o)

DEMOS    = di_demo orm_demo mvc_demo

.PHONY: all test clean

all: $(DEMOS)

$(OUTDIR):
	mkdir -p $(OUTDIR)

di_demo: $(SRCDIR)/di_container.o $(EXDIR)/di_demo.c | $(OUTDIR)
	$(CC) $(CFLAGS) $(SRCDIR)/di_container.o $(EXDIR)/di_demo.c -o $(OUTDIR)/di_demo $(LDFLAGS)

orm_demo: $(SRCDIR)/orm_core.o $(EXDIR)/orm_demo.c | $(OUTDIR)
	$(CC) $(CFLAGS) $(SRCDIR)/orm_core.o $(EXDIR)/orm_demo.c -o $(OUTDIR)/orm_demo $(LDFLAGS)

mvc_demo: $(SRCDIR)/mvc_pattern.o $(SRCDIR)/validator.o $(SRCDIR)/serializer.o $(EXDIR)/mvc_demo.c | $(OUTDIR)
	$(CC) $(CFLAGS) $(SRCDIR)/mvc_pattern.o $(SRCDIR)/validator.o $(SRCDIR)/serializer.o $(EXDIR)/mvc_demo.c -o $(OUTDIR)/mvc_demo $(LDFLAGS)

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

test: all
	@echo "Running DI Container Demo..."
	$(OUTDIR)/di_demo
	@echo ""
	@echo "Running ORM Core Demo..."
	$(OUTDIR)/orm_demo
	@echo ""
	@echo "Running MVC Pattern Demo..."
	$(OUTDIR)/mvc_demo
	@echo ""
	@echo "All demos completed."

clean:
	rm -rf $(OUTDIR)
	rm -f $(SRCDIR)/*.o
