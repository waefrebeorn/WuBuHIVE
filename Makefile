# WuBuHIVE -- the hive memory substrate, extracted for reuse across all wubu.
#
#   hive     linked fixed blocks + skipfield + freelist: stable slots, O(1)
#            amortised insert, O(1) erase, cache-friendly iteration
#   tagheap  Naughty Dog tagged-lifetime heaps over hive memory, per-thread
#   budget   named byte budgets that fail the build when exceeded
#   frame    bump allocator reset once per frame
#   tls      one pointer per thread, portably
#   pool     worker pool whose job queue is hive memory
#
# C11. MinGW GCC, GCC and Clang all build it.

# make's built-in CC default is `cc`, which does not exist on this box; set it
# explicitly. A command-line `make CC=clang` still overrides this.
CC = gcc
AR ?= ar
CFLAGS  ?= -std=c11 -O2 -Wall -Wextra
CFLAGS  += -Iinclude
LDLIBS  += -lm

# pthreads: winpthreads on MinGW. This machine's Code Integrity policy blocks
# libwinpthread-1.dll, so any gate that links threads must link winpthreads
# statically -- only -static removes the dependency (the static libpthread.a
# still imports it through emutls).
PTHREAD := -static

SRC      := src/wb_hive.c src/wb_tagheap.c src/wb_budget.c src/wb_frame.c src/wb_tls.c
THREAD_SRC := src/wb_pool.c

LIB      := build/libwubuhive.a
OBJ      := $(patsubst src/%.c,build/%.o,$(SRC) $(THREAD_SRC))

GATES    := test-hive test-tagheap test-budget test-frame test-atomic test-tls \
            test-pool test-threadheap

.PHONY: all test $(GATES) clean

all: $(LIB)

$(LIB): $(OBJ)
	ar rcs $@ $^

build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build:
	mkdir -p build

# ---- gates ----------------------------------------------------------------

test: $(GATES)

# test-hive: the substrate itself
build/test_hive.exe: tests/wb_hive_test.c src/wb_hive.c | build
	$(CC) $(CFLAGS) $^ -o $@
test-hive: build/test_hive.exe
	./$<

# test-tagheap: tagged lifetimes, budgets, per-thread heaps
build/test_tagheap.exe: tests/wb_tagheap_test.c src/wb_tagheap.c src/wb_hive.c src/wb_tls.c | build
	$(CC) $(CFLAGS) $^ -o $@
test-tagheap: build/test_tagheap.exe
	./$<

# test-budget: named byte budgets
build/test_budget.exe: tests/wb_budget_test.c src/wb_budget.c | build
	$(CC) $(CFLAGS) $^ -o $@
test-budget: build/test_budget.exe
	./$<

# test-frame: the per-frame bump allocator
build/test_frame.exe: tests/wb_frame_test.c src/wb_frame.c | build
	$(CC) $(CFLAGS) $^ -o $@
test-frame: build/test_frame.exe
	./$<

# test-atomic: memory-ordering wrappers
build/test_atomic.exe: tests/wb_atomic_test.c | build
	$(CC) $(CFLAGS) $(PTHREAD) $^ -o $@
test-atomic: build/test_atomic.exe
	./$<

# test-tls: the portable thread-local slot
build/test_tls.exe: tests/wb_tls_test.c src/wb_tls.c | build
	$(CC) $(CFLAGS) $(PTHREAD) $^ -o $@
test-tls: build/test_tls.exe
	./$<

# test-pool: worker pool on hive memory (threaded)
build/test_pool.exe: tests/wb_pool_test.c src/wb_pool.c src/wb_hive.c | build
	$(CC) $(CFLAGS) $(PTHREAD) $^ -o $@
test-pool: build/test_pool.exe
	./$<

# test-threadheap: per-thread tag heaps (threaded)
build/test_threadheap.exe: tests/wb_threadheap_test.c src/wb_tagheap.c src/wb_hive.c src/wb_tls.c | build
	$(CC) $(CFLAGS) $(PTHREAD) $^ -o $@
test-threadheap: build/test_threadheap.exe
	./$<

clean:
	rm -rf build
