CC      ?= cc
CFLAGS  ?= -O1 -g -Wall -Wextra -std=c11
LDFLAGS ?= -llzma

fv-unwrap: fv-unwrap.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

clean:
	rm -f fv-unwrap *.o
