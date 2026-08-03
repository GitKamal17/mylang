# Changelog

All notable changes to MyLang are documented here.

## [0.1.0] — 2026-08-02

Initial release.

### Core compiler (lexer → parser → codegen)

- `.vibes` transpiler to C99: lexer, parser, AST, and C code generator
- General-purpose features: `fn` functions, `return`, `print`, typed variable
  inference (`int` / `double` / `const char*`)
- Control flow: `if`, `else if`, `else`, `while`, `for`, `break`, `continue`
- Lists/arrays (`[1, 2, 3]`), dictionaries (`{"k": v}`), index access
- User input: `input("prompt")`
- File I/O: `read_file`, `write_file`, `append_file`, `file_exists`
- String methods: `.len`, `.split`, `.contains`, `.replace`, `.upper`,
  `.lower`, `.trim`; string concatenation via `+`
- Math & stdlib: `sqrt`, `abs`, `sin`, `cos`, `tan`, `floor`, `ceil`, `round`,
  `min`, `max`, `clamp`, `random`, `random_int`, `to_int`, `to_double`,
  `to_string`, `sleep`, `time`
- Conditional includes (`<stdio.h>`, `<stdlib.h>`, `<string.h>`, `<math.h>`,
  `<time.h>`) based on feature usage

### Game scripting (Raylib)

- `window`, `on update` blocks, `circle`, `draw_rect`
- Sprites: `sprite` + `draw_sprite` (Texture2D)
- Sound & music: `play_sound`, `play_music` (InitAudioDevice)
- Collision: `collide_circle`, `collide_rect`, `collide_point`
- Input: 20+ key checks, `mouse_x`, `mouse_y`, `mouse_down`, `mouse_click`

### Tooling

- CLI: `emit`, `build`, `run`, `debug`, `step`, `visualize`, `--explain`,
  `--version`, `install`, `list`
- Bare filenames not found in the current folder auto-resolve to `examples/`
  (e.g. `mylang run breakout.vibes` works from the repo root)
- Lexer: Python-style indentation tracking — `TOKEN_INDENT`/`TOKEN_DEDENT`
  emitted at block boundaries (blank/comment-only lines skipped, DEDENTs at
  EOF; `make test` includes the standalone `test_indent` dumper)
- Indentation policy: spaces only — tabs are rejected in code entirely
  (in indentation or between tokens) with a clear lexer error; tabs inside
  string literals are preserved verbatim
- Block headers for every block construct — `fn`, `if`/`else`, `while`,
  `for`, `on`, `struct` — now use `:` followed by a newline (`fn f(a, b):`,
  `if x > 5:`, `while x:`, `else:`, `for (...):`, `on update:`,
  `struct Point:`); legacy `{` headers still parse but emit one clear
  migration error
- Block bodies (for every block construct: `fn`, `if`/`else`, `while`,
  `for`, `on`, `struct`) are now bounded by indentation — `TOKEN_INDENT`
  opens the body and `TOKEN_DEDENT` closes it, replacing the old
  "statements until `}`" loops. Legacy `{…}` bodies are still tolerated:
  the header already flags them, and a trailing legacy `}` is absorbed
  (parser-only; `ast.h` and `codegen.c` untouched)
- `mylang install` package manager with bundled `std` package
- `mylang debug` / `step` / `visualize` / `--explain` teaching tools
- Multi-error reporting with line numbers and `^^^` carets
- VS Code syntax highlighting extension (`vibes-vscode/`)
- 110 automated tests (lexer / parser / codegen / indentation tracker)
- Makefile, MIT license, demo GIF, this changelog
