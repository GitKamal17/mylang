# MyLang — Makefile
#
# Targets:
#   make            Build the `mylang` CLI
#   make test       Build and run all test suites
#   make sanitize   Rebuild + run everything under ASan/UBSan
#   make analyze    Run clang's static analyzer over the whole toolchain
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
TEST_INDENT_BIN   = test_indent

.PHONY: all test test-cli test-extension install uninstall clean demo-gif sanitize analyze

all: mylang

mylang: $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDE) -c $< -o $@

test: $(TEST_LEXER_BIN) $(TEST_PARSER_BIN) $(TEST_CODEGEN_BIN) $(TEST_INDENT_BIN)
	./$(TEST_LEXER_BIN)
	./$(TEST_PARSER_BIN)
	./$(TEST_CODEGEN_BIN)
	./$(TEST_INDENT_BIN)

$(TEST_LEXER_BIN): tests/test_lexer.c src/lexer.c src/lexer.h
	$(CC) $(CFLAGS) $(INCLUDE) tests/test_lexer.c src/lexer.c -o $@ $(LDLIBS)

$(TEST_INDENT_BIN): tests/test_indent.c src/lexer.c src/lexer.h
	$(CC) $(CFLAGS) $(INCLUDE) tests/test_indent.c src/lexer.c -o $@ $(LDLIBS)

$(TEST_PARSER_BIN): tests/test_parser.c src/parser.c src/lexer.c src/parser.h src/lexer.h src/ast.h
	$(CC) $(CFLAGS) $(INCLUDE) tests/test_parser.c src/parser.c src/lexer.c -o $@ $(LDLIBS)

$(TEST_CODEGEN_BIN): tests/test_codegen.c src/codegen.c src/parser.c src/lexer.c src/codegen.h src/parser.h src/lexer.h src/ast.h
	$(CC) $(CFLAGS) $(INCLUDE) tests/test_codegen.c src/codegen.c src/parser.c src/lexer.c -o $@ $(LDLIBS)

# ---------------------------------------------------------------------------
# Security tooling
# ---------------------------------------------------------------------------
# `make sanitize` — rebuild the toolchain and test suites under
# AddressSanitizer + UBSan, then run everything (tests + real transpilations)
# under the sanitizers.
SANFLAGS = -std=c99 -Wall -Wextra -g -O1 -fsanitize=address,undefined \
           -fno-omit-frame-pointer -D_POSIX_C_SOURCE=200809L

sanitize: $(TEST_LEXER_BIN)_san $(TEST_PARSER_BIN)_san $(TEST_CODEGEN_BIN)_san $(TEST_INDENT_BIN)_san mylang_san
	./$(TEST_LEXER_BIN)_san
	./$(TEST_PARSER_BIN)_san
	./$(TEST_CODEGEN_BIN)_san
	./$(TEST_INDENT_BIN)_san
	./mylang_san emit examples/app.vibes > /dev/null
	./mylang_san emit examples/game.vibes > /dev/null
	./mylang_san debug examples/game.vibes > /dev/null
	@echo "Sanitizer run: all clean (ASan + UBSan)"

$(TEST_LEXER_BIN)_san: tests/test_lexer.c src/lexer.c src/lexer.h
	$(CC) $(SANFLAGS) $(INCLUDE) tests/test_lexer.c src/lexer.c -o $@ $(LDLIBS)

$(TEST_PARSER_BIN)_san: tests/test_parser.c src/parser.c src/lexer.c src/parser.h src/lexer.h src/ast.h
	$(CC) $(SANFLAGS) $(INCLUDE) tests/test_parser.c src/parser.c src/lexer.c -o $@ $(LDLIBS)

$(TEST_CODEGEN_BIN)_san: tests/test_codegen.c src/codegen.c src/parser.c src/lexer.c src/codegen.h src/parser.h src/lexer.h src/ast.h
	$(CC) $(SANFLAGS) $(INCLUDE) tests/test_codegen.c src/codegen.c src/parser.c src/lexer.c -o $@ $(LDLIBS)

$(TEST_INDENT_BIN)_san: tests/test_indent.c src/lexer.c src/lexer.h
	$(CC) $(SANFLAGS) $(INCLUDE) tests/test_indent.c src/lexer.c -o $@ $(LDLIBS)

mylang_san: $(SRCS)
	$(CC) $(SANFLAGS) $(INCLUDE) $(SRCS) -o $@ $(LDLIBS)

# `make analyze` — run clang's static analyzer over every source file.
# Fails if the analyzer emits any warning (clang --analyze exits 0 on
# findings, so we grep its output explicitly).
analyze:
	@for f in $(SRCS); do \
	  echo "== $$f =="; \
	  OUT="$$(clang --analyze -Xanalyzer -analyzer-output=text $(INCLUDE) \
	        -D_POSIX_C_SOURCE=200809L $$f 2>&1)"; \
	  RC=$$?; \
	  printf '%s\n' "$$OUT"; \
	  if [ $$RC -ne 0 ] || printf '%s\n' "$$OUT" | grep -q 'warning:'; then \
	    echo "analyzer findings in $$f"; exit 1; \
	  fi; \
	done
	@echo "Static analysis: no findings"

install: mylang
	install -d $(BINDIR)
	install -m 0755 mylang $(BINDIR)/mylang
	@echo "Installed mylang $(VERSION) -> $(BINDIR)/mylang"

uninstall:
	rm -f $(BINDIR)/mylang

clean:
	rm -f mylang $(OBJS) $(DEPS) $(TEST_LEXER_BIN) $(TEST_PARSER_BIN) $(TEST_CODEGEN_BIN) $(TEST_INDENT_BIN) \
	      $(TEST_LEXER_BIN).d $(TEST_PARSER_BIN).d $(TEST_CODEGEN_BIN).d $(TEST_INDENT_BIN).d \
	      mylang_san $(TEST_LEXER_BIN)_san $(TEST_PARSER_BIN)_san $(TEST_CODEGEN_BIN)_san $(TEST_INDENT_BIN)_san \
	      src/*.o.tmp

demo-gif: mylang
	bash scripts/make_demo_gif.sh

# End-to-end checks compile and execute generated C (Python 3 required).
test-cli: mylang
	python3 tests/test_cli.py

test-extension: mylang
	npm test --prefix vibes-vscode
