# Contributing to MyLang

Thanks for helping build MyLang! This guide covers how to build, test, and
contribute to the project.

## Project layout

```
src/
  lexer.h/c      Turns .vibes source text into tokens
  parser.h/c     Turns tokens into an Abstract Syntax Tree (AST)
  ast.h          AST node definitions (statements + expressions)
  codegen.h/c    Emits C code from the AST
  pipeline.h/c   Three-panel lexer → parser → codegen visualizer
  main.c         The `mylang` CLI (emit/build/run/debug/step/visualize/
                 explain/install/list)
examples/        Ready-to-run .vibes programs (app, game, pong, breakout, ...)
packages/        Source for bundled std packages (installed via `mylang install std`)
tests/           C test suites (test_lexer.c, test_parser.c, test_codegen.c)
vibes-vscode/    VS Code syntax highlighting extension for .vibes
LEARN.md         The official language documentation
```

## Building

Requirements: a C99 compiler (`cc`/`clang`), `make`, and `libm` (standard on
macOS and Linux). Raylib is only needed to *run* game-mode programs, not to
build the compiler itself. `make demo-gif` additionally needs `swiftc`
(shipped with macOS), `ffmpeg`, and `python3`.

```sh
make              # builds ./mylang
make clean        # removes binaries and object files
```

Or build by hand:

```sh
cc -std=c99 -Wall -Wextra -I src src/main.c src/codegen.c src/parser.c \
   src/lexer.c src/pipeline.c -o mylang -lm
```

## Testing

Run the full suite (lexer, parser, codegen):

```sh
make test
```

Each suite is a standalone C program. Run them individually:

```sh
make test_lexer && ./test_lexer
make test_parser && ./test_parser
make test_codegen && ./test_codegen
```

### Adding a test

Tests live in `tests/test_lexer.c`, `tests/test_parser.c`, and
`tests/test_codegen.c`. Each suite has a `tests_run/tests_pass/tests_fail`
counter and a `TEST(name)`/`PASS()`/`ASSERT_*` macro set.

1. Write a `static void test_my_feature(void)` function that:
   - calls the helper (e.g. `parse_string(...)` or `parse_and_codegen(...)`),
   - uses `ASSERT_*` macros to check the result,
   - ends with `PASS();` and frees any allocated memory.
2. Register it by calling `test_my_feature();` inside `int main(void)`.
3. Run `make test` — the suite prints `--- Results: X/Y passed, 0 failed ---`.

Codegen tests should assert on *substrings of the emitted C* (e.g.
`ASSERT_STR_CONTAINS(gen, "int distance(", ...)`), not exact whitespace.

## CLI smoke checks

```sh
./mylang --version
./mylang emit examples/app.vibes
./mylang run examples/app.vibes
./mylang debug examples/game.vibes    # tokens + AST
./mylang --explain tokens examples/game.vibes
./mylang visualize examples/game.vibes
./mylang install std && ./mylang list
```

## Code style

- C99, `-Wall -Wextra` must produce **zero warnings**.
- 4-space indentation, braces on the same line for control flow.
- `static` for everything not part of the public API.
- Public API lives in headers (`lexer.h`, `parser.h`, `ast.h`, `codegen.h`,
  `pipeline.h`) with a comment for each exported symbol.
- The Makefile injects the version via `-DMYLANG_VERSION`; keep the
  `#ifndef MYLANG_VERSION` fallback in `src/main.c`.

## Feature checklist

1. **Lexer** — add any new keywords/tokens in `src/lexer.c` (+ tests in
   `tests/test_lexer.c`).
2. **Parser** — build the AST nodes in `src/ast.h` + `src/parser.c`
   (+ tests in `tests/test_parser.c`).
3. **Codegen** — emit C in `src/codegen.c` (+ tests in `tests/test_codegen.c`).
4. **Docs** — update `LEARN.md` (keyword/builtin reference) and the VS Code
   grammar in `vibes-vscode/syntaxes/vibes.tmLanguage.json` if you added
   keywords.
5. **Verify** — `make` (zero warnings) and `make test` (all suites green).

## Pull request checklist

- [ ] `make` builds with zero warnings
- [ ] `make test` passes all suites
- [ ] New feature has lexer/parser/codegen tests as appropriate
- [ ] `LEARN.md` and the VS Code grammar are updated if keywords changed
- [ ] An example in `examples/` demonstrates the feature if it's user-facing
