# Cross-compile a small static aarch64 binary suitable for Android recovery's
# busybox env. Uses aarch64-linux-gnu-gcc with static-glibc.
# Build via project Docker image — see ../../scripts/make-graft-binary.sh
CC      ?= aarch64-linux-gnu-gcc
STRIP   ?= aarch64-linux-gnu-strip
# Override CC/STRIP on the command line for native builds: make CC=gcc STRIP=strip
CFLAGS  ?= -O2 -static -Wall -Wextra -std=c11

graft-vbmeta: graft-vbmeta.c
	$(CC) $(CFLAGS) $^ -o $@
	$(STRIP) $@ 2>/dev/null || true

clean:
	rm -f graft-vbmeta
