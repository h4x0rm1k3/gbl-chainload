CC      ?= cc
CFLAGS  ?= -O1 -g -Wall -Wextra -Wno-unused-parameter -std=c11 -D__HOST_BUILD__
PROJ    := $(realpath ../..)
DPL     := $(PROJ)/GblChainloadPkg/Library/DynamicPatchLib

INCLUDES := -I$(PROJ)/GblChainloadPkg/Include/Library \
            -I$(DPL)/Internal

TESTS := test_patch1

all: $(TESTS)
	@for t in $(TESTS); do echo "== $$t =="; ./$$t || exit 1; done

test_patch1: test_patch1.c \
             $(DPL)/Internal/ScanLib.c \
             $(DPL)/Internal/PeSections.c \
             $(DPL)/universal/universal.c
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@

clean:
	rm -f $(TESTS) *.o
