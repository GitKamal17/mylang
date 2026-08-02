# MyLang — Makefile
#
# Targets:
#   make            Build the `mylang` CLI
#   make test       Build and run all test suites
#   make install    Copy `mylang` to /usr/local/bin (override PREFIX)
#   make uninstall  Remove the installed binary
#   make clean      Remove build artifacts
#   make demo-gif   Render docs/demo.gif (requires ffmpeg + python3)
#
# Examples:
#   make
#   make test
#   sudo make install                 # -> /usr/local/bin/mylang
#   make install PREFIX=$HOME/.local  # -> $HOME/.local/bin/mylang

CC      ?= cc
CFLAGS  ?= -std=c99 -Wall -Wextra -O2
# Strict -std=c99 defines __STRICT_ANSI__, which hides POSIX declarations on
# glibc (Linux): pid_t, getpid, mkdir, stat, opendir, readdir, ... would all
# become unknown/implicit. Request POSIX.1-2008 explicitly so the same source
# compiles cleanly on macOS (clang) and Linux (gcc).
CFLAGS  += -D_POSIX_C_SOURCE=200809L
INCLUDE  = -I src
LDLIBS   = -lm
DEPFLAGS = -MMD -MP

VERSION := $(shell cat VERSION 2>/dev/null || echo dev)

# Inject the version at COMPILE time (main.c uses #ifndef MYLANG_VERSION).
# The \"...\" escapes survive make's variable expansion so the compiler
# sees a quoted string literal, not a bare floating constant.
CFLAGS += -DMYLANG_VERSION=\"$(VERSION)\" $(DEPFLAGS)

PREFIX  ?= /usr/local
BINDIR   = $(PREFIX)/bin

SRCS = src/main.c src/codegen.c src/parser.c src/lexer.c src/pipeline.c
OBJS = $(SRCS:.c=.o)
DEPS = $(OBJS:.o=.d)

# The default goal must be explicit: the special rule below would
# otherwise become make's first target and `make` would only build main.o.
.DEFAULT_GOAL := mylang

# Rebuild main.o when the version or build config changes (so an
# incremental `make` picks up VERSION edits).
src/main.o: Makefile VERSION

# Pull in auto-generated header dependencies (*.d)
-include $(DEPS)

TEST_LEXER_BIN    = test_lexer
TEST_PARSER_BIN   = test_parser
TEST_CODEGEN_BIN  = test_codegen

.PHONY: all test install uninstall clean demo-gif

all: mylang

mylang: $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDE) -c $< -o $@

test: $(TEST_LEXER_BIN) $(TEST_PARSER_BIN) $(TEST_CODEGEN_BIN)
	./$(TEST_LEXER_BIN)
	./$(TEST_PARSER_BIN)
	./$(TEST_CODEGEN_BIN)

$(TEST_LEXER_BIN): tests/test_lexer.c src/lexer.c src/lexer.h
	$(CC) $(CFLAGS) $(INCLUDE) tests/test_lexer.c src/lexer.c -o $@ $(LDLIBS)

$(TEST_PARSER_BIN): tests/test_parser.c src/parser.c src/lexer.c src/parser.h src/lexer.h src/ast.h
	$(CC) $(CFLAGS) $(INCLUDE) tests/test_parser.c src/parser.c src/lexer.c -o $@ $(LDLIBS)

$(TEST_CODEGEN_BIN): tests/test_codegen.c src/codegen.c src/parser.c src/lexer.c src/codegen.h src/parser.h src/lexer.h src/ast.h
	$(CC) $(CFLAGS) $(INCLUDE) tests/test_codegen.c src/codegen.c src/parser.c src/lexer.c -o $@ $(LDLIBS)

install: mylang
	install -d $(BINDIR)
	install -m 0755 mylang $(BINDIR)/mylang
	@echo "Installed mylang $(VERSION) -> $(BINDIR)/mylang"

uninstall:
	rm -f $(BINDIR)/mylang

clean:
	rm -f mylang $(OBJS) $(DEPS) $(TEST_LEXER_BIN) $(TEST_PARSER_BIN) $(TEST_CODEGEN_BIN) \
	      $(TEST_LEXER_BIN).d $(TEST_PARSER_BIN).d $(TEST_CODEGEN_BIN).d \
	      src/*.o.tmp

demo-gif: mylang
	bash scripts/make_demo_gif.sh
