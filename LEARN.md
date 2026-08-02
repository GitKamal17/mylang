<div align="center">

# 🎮 MyLang — The Vibes Language

**A tiny, human-friendly language that transpiles to C + Raylib.**

Write games in `.vibes`, generate real C99, compile with `clang -lraylib`, and run — or use it as a general-purpose scripting language with functions, variables, arrays, dictionaries, and file I/O.

**Two ways to learn:**
- **[📖 PART 1 — Teaching Path](#-part-1--the-teaching-path)** — see exactly how your code becomes C, stage by stage
- **[🕹️ PART 2 — Game Scripting Path](#-part-2--the-game-scripting-path)** — build real games: Pong, Breakout, and a Platformer

</div>

---

## 🔧 Installation

### Prerequisites

| Tool | Why |
|---|---|
| **clang** (or gcc) | C99 compiler for the generated code |
| **Raylib** | Graphics library (`brew install raylib` on macOS) |
| **make** (optional) | Build convenience |

### Build MyLang

```bash
cd mylang

# Build the CLI binary (note: pipeline.c is part of the build)
cc -std=c99 -Wall -Wextra -I src \
    src/main.c src/codegen.c src/parser.c src/lexer.c src/pipeline.c \
    -o mylang -lm

# Optional: install to PATH
sudo cp mylang /usr/local/bin/
```

### Quick Verification

```bash
./mylang --help
```

You should see:

```
Usage: mylang <command> <file.vibes>

Commands:
  emit   <file>    Parse and print generated C to stdout
  build  <file>    Parse, generate C, and compile to binary
  run    <file>    Parse, generate C, compile, and run
  debug  <file>    Print tokens and AST tree to stderr
  step   <file>    Step-through: tokens, then AST, then generated C
  visualize <file>  Show the lexer → parser → codegen pipeline diagram
  --explain emit   <file>    Show annotated C code with source-line comments
  --explain tokens <file>    Show each token with plain-English explanations
```

---

# 📖 PART 1 — The Teaching Path

> **Goal:** understand *how* MyLang works by watching each stage transform your code.

Every `.vibes` file passes through a **three-stage pipeline** before it becomes a running program:

```
.vibes source
    │
    ▼
┌─────────────┐   Stage 1    turns characters into a list of
│   LEXER     │              meaningful units called TOKENS
└─────────────┘
    │  token stream
    ▼
┌─────────────┐   Stage 2    turns tokens into a tree that shows
│   PARSER    │              the STRUCTURE of your program (the AST)
└─────────────┘
    │  abstract syntax tree
    ▼
┌─────────────┐   Stage 3    walks the tree and prints real C99 code
│  CODEGEN    │              with Raylib calls
└─────────────┘
    │  C source
    ▼
┌─────────────┐
│    CLI      │   compiles with clang and runs
└─────────────┘
```

MyLang gives you **five teaching commands** that let you peek inside each stage.

---

## 🪜 Stage 1 — The Lexer: `mylang debug` (tokens)

The lexer (`src/lexer.c`) reads your file and splits it into tokens — the smallest meaningful pieces: keywords, identifiers, numbers, strings, and operators.

Run this on the example game:

```bash
mylang debug examples/game.vibes
```

The first half of the output is the **token stream**:

```
=== TOKENS ===
  [1:1] KEYWORD      "window"
  [1:9] STRING       "Movable Ball"
  [1:22] COMMA        ","
  [1:24] NUMBER       "800"
  [1:29] NUMBER       "600"
  [3:1] IDENTIFIER   "x"
  [3:3] EQUALS       "="
  [3:5] NUMBER       "400"
  [6:1] KEYWORD      "if"
  [6:4] KEYWORD      "key_right"
  [6:14] BRACE_OPEN   "{"
  [6:16] IDENTIFIER   "x"
  [6:22] PLUS         "+"
  [6:24] NUMBER       "5"
  [6:26] BRACE_CLOSE  "}"
  ...
```

Each line shows the **position** (`[line:column]`), the **token type**, and the **text** it matched. Notice:

- `window` is a **KEYWORD** (a reserved word)
- `x` is an **IDENTIFIER** (a variable or function name)
- `800` is a **NUMBER**, `"Movable Ball"` is a **STRING**
- `=` is **EQUALS**, `+` is **PLUS**, `{` / `}` are **BRACE_OPEN** / **BRACE_CLOSE**

### Try it with plain English: `mylang --explain tokens`

Want each token *explained* instead of just named? Use the explain mode:

```bash
mylang --explain tokens examples/game.vibes
```

```
Token Explanations for: examples/game.vibes
[L:C]    TYPE         TEXT                  EXPLANATION
-------- ------------ --------------------  -------------------------------------
[1:1]    KEYWORD      window                Keyword — a reserved word like 'if', 'while', 'print', 'fn'
[1:9]    STRING       Movable Ball          String — a text literal inside double quotes (e.g. "hello")
[1:24]   NUMBER       800                   Number — a numeric literal (e.g. 42, 3.14)
[3:3]    EQUALS       =                     = — Assignment operator. Stores a value in a variable
[6:4]    KEYWORD      key_right             Keyword — a reserved word like 'if', 'while', 'print', 'fn'
[6:14]   BRACE_OPEN   {                     { — opens a block of code (e.g. if/while/function body)
[6:22]   PLUS         +                     + — Addition operator. Adds two numbers or concatenates strings
```

> 💡 **Lexer lesson:** `window "Title", 800, 600` is just a sequence of tokens. The lexer does *not* understand what a "window" is — it only knows it's a reserved word. Meaning comes later.

---

## 🪜 Stage 2 — The Parser: `mylang debug` (AST)

The parser (`src/parser.c`) takes the token stream and builds an **Abstract Syntax Tree (AST)** — a tree that captures the *structure* of your program.

The second half of `mylang debug` prints that tree:

```
=== AST ===
  WINDOW
    STRING("Movable Ball")
    NUMBER(800)
    NUMBER(600)
  ASSIGN(x)
    NUMBER(400)
  ASSIGN(y)
    NUMBER(300)
  IF
    IDENT(key_right)
  THEN:
      ASSIGN(x)
        BINARY(+)
          IDENT(x)
          NUMBER(5)
  IF
    IDENT(key_left)
  THEN:
      ASSIGN(x)
        BINARY(-)
          IDENT(x)
          NUMBER(5)
  CALL(circle, 4 args)
    IDENT(x)
    IDENT(y)
    NUMBER(30)
    STRING("red")
```

Read it from the top: the program has a `WINDOW` statement, two `ASSIGN`ments (`x`, `y`), two `IF` statements (each with a `THEN` branch containing an `ASSIGN` with a `BINARY` expression), and a `CALL` to `circle` with four arguments.

> 💡 **Parser lesson:** the AST shows *relationships*. `x = x + 5` inside `if key_right` is now a tree: `IF → ASSIGN(x) → BINARY(+) → (IDENT(x), NUMBER(5))`. Structure, not text.

---

## 🪜 Stage 3 — The Code Generator: `mylang emit`

The code generator (`src/codegen.c`) walks the AST and prints real C. This is the stage where MyLang becomes "magic":

```bash
mylang emit examples/game.vibes
```

```c
#include "raylib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    InitWindow(800, 600, "Movable Ball");
    SetTargetFPS(60);

    int x = 400;
    int y = 300;

    while (!WindowShouldClose()) {
        if (IsKeyDown(KEY_RIGHT)) {
            x = (x + 5);
        }
        if (IsKeyDown(KEY_LEFT)) {
            x = (x - 5);
        }

        BeginDrawing();
        ClearBackground(BLACK);
        DrawCircle(x, y, 30, RED);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
```

Notice how the codegen **reorders** your statements into the correct C structure:

| Your `.vibes` code | Generated C |
|---|---|
| `window "Movable Ball", 800, 600` | `InitWindow(800, 600, "Movable Ball");` before the loop |
| `x = 400` / `y = 300` | `int x = 400; int y = 300;` before the loop |
| `if key_right { x = x + 5 }` | `if (IsKeyDown(KEY_RIGHT)) { ... }` inside the loop, *before drawing* |
| `circle x, y, 30, "red"` | `DrawCircle(x, y, 30, RED);` inside `BeginDrawing()` / `EndDrawing()` |

### See which line each C line came from: `mylang --explain emit`

```bash
mylang --explain emit examples/game.vibes
```

```c
int main(void) {
    InitWindow(800, 600, "Movable Ball");
    SetTargetFPS(60);

    int x = 400;
    int y = 300;

    while (!WindowShouldClose()) {
        if (IsKeyDown(KEY_RIGHT)) {
// [game.vibes:6]
            x = (x + 5);
        }
        if (IsKeyDown(KEY_LEFT)) {
// [game.vibes:7]
            x = (x - 5);
        }
        ...
        DrawCircle(x, y, 30, RED);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
```

Each generated line is annotated with `// [file.vibes:line]` — the exact source line it came from. Great for tracing bugs backward.

---

## 🖼️ The Big Picture: `mylang visualize`

Want all three stages in one animated-looking diagram? 

```bash
mylang visualize examples/game.vibes
```

```
  ═══════════════════════════════════════════════════════
     PIPELINE VISUALIZATION: game.vibes
  ═══════════════════════════════════════════════════════

  +-----------------------------------------------------+
  |   1. LEXER  (tokenizer)                               |
  |   Reads line 1: "window "Movable Ball", 800, 600"    |
  |   Produces 6 tokens: KEYWORD('window') STRING(...)   |
  +-----------------------------------------------------+
       |
       v
  +-----------------------------------------------------+
  |   2. PARSER  (builds AST)                             |
  |   Statement type: WINDOW(title, w, h) → InitWindow() |
  +-----------------------------------------------------+
       |
       v
  +-----------------------------------------------------+
  |   3. CODEGEN  (emits C)                               |
  |   InitWindow(800, 600, "Movable Ball");              |
  |   SetTargetFPS(60);                                  |
  |   int x = 400;                                       |
  +-----------------------------------------------------+

  The generated C is then compiled with clang and run.
  Run 'mylang emit game.vibes' to see the full C output.
```

---

## 👣 The Step-Through: `mylang step`

For the slowest, most thorough walk-through, `mylang step` pauses between every single token, then every AST node, then every generated C line:

```bash
mylang step examples/game.vibes
```

Press **Enter** to advance one step at a time. Tokens print first, then the AST, then the generated C — all to stderr, so stdout stays clean for redirecting.

---

## ✅ Teaching Path Recap

| Command | What it shows |
|---|---|
| `mylang debug file.vibes` | Raw tokens + full AST tree |
| `mylang --explain tokens file.vibes` | Each token with a plain-English explanation |
| `mylang --explain emit file.vibes` | Generated C with `// source-file:line` annotations |
| `mylang visualize file.vibes` | Three-panel lexer → parser → codegen diagram |
| `mylang step file.vibes` | Interactive step-through of all three stages |
| `mylang emit file.vibes` | Just the final C output |

---

# 🕹️ PART 2 — The Game Scripting Path

> **Goal:** build real, playable games as fast as possible.

## 🚀 Quick Start

Create `hello.vibes`:

```vibes
window "Hello Vibes", 800, 600

circle 400, 300, 100, "skyblue"
```

Run it:

```bash
mylang run hello.vibes
```

A window opens with a big sky-blue circle. That's it — two lines.

---

## 🪟 The Window

```vibes
window "Title", width, height
```

Every game starts with one. It becomes `InitWindow(width, height, "Title")` + a `while (!WindowShouldClose())` loop that runs at 60 FPS.

Inside that loop, MyLang automatically sorts your code into two phases:

1. **Update phase** — variable changes, `if key_*` input checks, collisions, sound (`play_sound`, `UpdateMusicStream`). Runs *before* drawing.
2. **Draw phase** — `circle`, `draw_rect`, `draw_sprite`. Runs inside `BeginDrawing()` / `EndDrawing()`.

You never write `while`, `BeginDrawing`, or `EndDrawing` yourself — MyLang does it for you.

---

## 📦 Variables

```vibes
x = 400
y = 300
score = 0
speed = 2.5
name = "Kamal"
```

Generated C:

```c
int x = 400;
int y = 300;
int score = 0;
double speed = 2.5;
const char *name = "Kamal";
```

Types are **inferred**: integers become `int`, decimals become `double`, strings become `const char *`.

Update variables any time:

```vibes
x = x + 5
score = score + 250
```

> ⚠️ Variable names must be identifiers — not keywords, not starting with a digit.

---

## ⌨️ Input Controls

### Keyboard keys

```vibes
if key_right { x = x + 5 }
if key_space  { shoot() }
```

| MyLang | Generated C |
|---|---|
| `key_right` | `IsKeyDown(KEY_RIGHT)` |
| `key_left` | `IsKeyDown(KEY_LEFT)` |
| `key_up` | `IsKeyDown(KEY_UP)` |
| `key_down` | `IsKeyDown(KEY_DOWN)` |
| `key_space` | `IsKeyDown(KEY_SPACE)` |
| `key_enter` | `IsKeyDown(KEY_ENTER)` |
| `key_w` / `key_a` / `key_s` / `key_d` | `IsKeyDown(KEY_W)` / `KEY_A` / `KEY_S` / `KEY_D` |

### Mouse

```vibes
if mouse_down  { x = mouse_x }      # left button held → IsMouseButtonDown(MOUSE_LEFT_BUTTON)
if mouse_click { y = mouse_y }      # just pressed → IsMouseButtonPressed(MOUSE_LEFT_BUTTON)
```

| MyLang | Generated C |
|---|---|
| `mouse_x` | `GetMouseX()` (usable anywhere: `circle mouse_x, mouse_y, 5, "red"`) |
| `mouse_y` | `GetMouseY()` |
| `mouse_down` | `IsMouseButtonDown(MOUSE_LEFT_BUTTON)` |
| `mouse_click` | `IsMouseButtonPressed(MOUSE_LEFT_BUTTON)` |

---

## 🎨 Drawing Commands

### `circle x, y, radius, "color"`

```vibes
circle 400, 300, 50, "red"          →  DrawCircle(400, 300, 50, RED);
circle x, y, 30, "skyblue"          →  DrawCircle(x, y, 30, SKYBLUE);
circle mouse_x, mouse_y, 8, "blue"  →  DrawCircle(GetMouseX(), GetMouseY(), 8, BLUE);
```

`draw_circle` is an alias with the exact same output.

### `draw_rect x, y, width, height, "color"`

```vibes
draw_rect 20, 50, 100, 25, "white"  →  DrawRectangle(20, 50, 100, 25, WHITE);
draw_rect px, py, 40, 40, "lime"    →  DrawRectangle(px, py, 40, 40, LIME);
```

### `sprite` + `draw_sprite`

```vibes
sprite player = "player.png"        →  Texture2D player = LoadTexture("player.png");
draw_sprite player, x, y            →  DrawTexture(player, x, y, WHITE);
```

`draw_sprite` uses the sprite's variable name, so you can move it with variables.

### Colors (24 built-in)

| MyLang | C | MyLang | C |
|---|---|---|---|
| `"red"` | `RED` | `"purple"` | `PURPLE` |
| `"green"` | `GREEN` | `"pink"` | `PINK` |
| `"blue"` | `BLUE` | `"brown"` | `BROWN` |
| `"white"` | `WHITE` | `"gray"` / `"grey"` | `GRAY` |
| `"black"` | `BLACK` | `"lightgray"` | `LIGHTGRAY` |
| `"yellow"` | `YELLOW` | `"darkgray"` | `DARKGRAY` |
| `"orange"` | `ORANGE` | `"skyblue"` | `SKYBLUE` |
| `"maroon"` | `MAROON` | `"violet"` | `VIOLET` |
| `"darkgreen"` | `DARKGREEN` | `"lime"` | `LIME` |
| `"gold"` | `GOLD` | `"darkbrown"` | `DARKBROWN` |
| `"beige"` | `BEIGE` | `"magenta"` | `MAGENTA` |
| `"turquoise"` | `TURQUOISE` | | |

---

## 🔊 Sound

```vibes
jump = load_sound("jump.wav")       →  Sound jump = LoadSound("jump.wav");   (before the loop)
play_sound(jump)                    →  PlaySound(jump);                     (before BeginDrawing)

play_music("bgm.ogg")               →  InitAudioDevice();
                                       Music bgm = LoadMusicStream("bgm.ogg");
                                       PlayMusicStream(bgm);                 (before the loop)
                                       UpdateMusicStream(bgm);               (inside the loop)
```

The music variable name is derived from the filename (`"music/bgm.ogg"` → `bgm`). Audio is automatically cleaned up (`UnloadSound`, `UnloadMusicStream`, `CloseAudioDevice`) before the window closes.

---

## 💥 Collision Detection

All three return true/false, so they work directly as `if` conditions:

```vibes
if collide_circle(x1, y1, r1, x2, y2, r2) { ... }
# → if (CheckCollisionCircles((Vector2){x1, y1}, r1, (Vector2){x2, y2}, r2))

if collide_rect(x1, y1, w1, h1, x2, y2, w2, h2) { ... }
# → if (CheckCollisionRecs((Rectangle){x1, y1, w1, h1}, (Rectangle){x2, y2, w2, h2}))

if collide_point(x, y, rx, ry, rw, rh) { ... }
# → if (CheckCollisionPointRec((Vector2){x, y}, (Rectangle){rx, ry, rw, rh}))
```

---

## 🎪 Complete Example Games

Three fully playable games ship in `examples/`. Run any of them with `mylang run examples/<name>.vibes`.

> 🎨 **Why no sprites or sound in the games?** Pong, Breakout, and the Platformer deliberately use only `circle` + `draw_rect` so they run with **zero asset files**. Sprite and audio demos live in `examples/sprite.vibes` and `examples/sound.vibes` — add `load_sound`/`play_sound` to any game by following the pattern there.

### 🏓 Pong — `examples/pong.vibes`

Two-player paddle game. **Player 1:** `W` / `S`. **Player 2:** arrows.

```vibes
window "Pong", 800, 600

ball_x = 400
ball_y = 300
ball_dx = 3
ball_dy = 2

p1_y = 300
p2_y = 300

score1 = 0
score2 = 0

on update {
    if key_w { p1_y = p1_y - 6 }
    if key_s { p1_y = p1_y + 6 }
    if key_up { p2_y = p2_y - 6 }
    if key_down { p2_y = p2_y + 6 }

    if p1_y < 50 { p1_y = 50 }
    if p1_y > 550 { p1_y = 550 }
    if p2_y < 50 { p2_y = 50 }
    if p2_y > 550 { p2_y = 550 }

    ball_x = ball_x + ball_dx
    ball_y = ball_y + ball_dy

    if ball_y < 10 { ball_dy = 0 - ball_dy }
    if ball_y > 590 { ball_dy = 0 - ball_dy }

    if collide_rect(ball_x - 10, ball_y - 10, 20, 20, 20, p1_y - 50, 12, 100) {
        ball_dx = 0 - ball_dx
    }
    if collide_rect(ball_x - 10, ball_y - 10, 20, 20, 768, p2_y - 50, 12, 100) {
        ball_dx = 0 - ball_dx
    }

    if ball_x < 0 {
        score2 = score2 + 1
        ball_x = 400
        ball_y = 300
        ball_dx = 0 - ball_dx
    }
    if ball_x > 800 {
        score1 = score1 + 1
        ball_x = 400
        ball_y = 300
        ball_dx = 0 - ball_dx
    }
}

circle ball_x, ball_y, 10, "white"
draw_rect 20, p1_y - 50, 12, 100, "skyblue"
draw_rect 768, p2_y - 50, 12, 100, "red"
```

**What it teaches:** `on update { }` blocks, variable updates, `draw_rect`, and `collide_rect` for paddle/ball bounces.

### 🧱 Breakout — `examples/breakout.vibes`

Brick breaker with `collide_rect` on every brick. Bricks "die" by flying off-screen (`y = 1000`).

```vibes
window "Breakout", 800, 600

pad_x = 350
ball_x = 400
ball_y = 500
ball_dx = 3
ball_dy = -3

b1x = 100
b1y = 80
b2x = 250
b2y = 80
b3x = 400
b3y = 80
b4x = 550
b4y = 80
b5x = 175
b5y = 140
b6x = 325
b6y = 140
b7x = 475
b7y = 140

score = 0

on update {
    if key_left { pad_x = pad_x - 8 }
    if key_right { pad_x = pad_x + 8 }
    if pad_x < 0 { pad_x = 0 }
    if pad_x > 680 { pad_x = 680 }

    ball_x = ball_x + ball_dx
    ball_y = ball_y + ball_dy

    if ball_x < 8 { ball_dx = 0 - ball_dx }
    if ball_x > 792 { ball_dx = 0 - ball_dx }
    if ball_y < 8 { ball_dy = 0 - ball_dy }

    if collide_rect(ball_x - 8, ball_y - 8, 16, 16, pad_x, 570, 120, 14) {
        ball_dy = 0 - ball_dy
    }

    if collide_rect(ball_x - 8, ball_y - 8, 16, 16, b1x, b1y, 100, 30) {
        b1y = 1000
        ball_dy = 0 - ball_dy
        score = score + 100
    }
    # ... same pattern for b2x/b2y through b7x/b7y ...
    # (full file: examples/breakout.vibes)

    if ball_y > 620 {
        ball_x = 400
        ball_y = 500
        ball_dy = -3
    }
}

draw_rect pad_x, 570, 120, 14, "skyblue"
draw_rect b1x, b1y, 100, 30, "red"
draw_rect b2x, b2y, 100, 30, "orange"
draw_rect b3x, b3y, 100, 30, "yellow"
draw_rect b4x, b4y, 100, 30, "green"
draw_rect b5x, b5y, 100, 30, "blue"
draw_rect b6x, b6y, 100, 30, "purple"
draw_rect b7x, b7y, 100, 30, "pink"
circle ball_x, ball_y, 8, "white"
```

**What it teaches:** per-object collision checks, scoring, and the off-screen trick for "removing" objects.

### 🏃 Platformer — `examples/platformer.vibes`

Gravity, jumping, and platforms. Arrows move, **Space** jumps.

```vibes
window "Platformer", 800, 600

px = 100
py = 100
vy = 0
on_ground = 0

on update {
    vy = vy + 1
    py = py + vy

    if key_right { px = px + 5 }
    if key_left  { px = px - 5 }

    if px < 0 { px = 0 }
    if px > 760 { px = 760 }

    if collide_rect(px, py, 40, 40, 0, 560, 800, 40) {
        py = 560 - 40
        vy = 0
        on_ground = 1
    }
    if collide_rect(px, py, 40, 40, 250, 400, 200, 20) {
        py = 400 - 40
        vy = 0
        on_ground = 1
    }
    if collide_rect(px, py, 40, 40, 550, 300, 180, 20) {
        py = 300 - 40
        vy = 0
        on_ground = 1
    }

    if key_space {
        if on_ground == 1 {
            vy = 0 - 14
            on_ground = 0
        }
    }

    if py > 700 {
        px = 100
        py = 100
        vy = 0
    }
}

draw_rect px, py, 40, 40, "skyblue"
draw_rect 0, 560, 800, 40, "lime"
draw_rect 250, 400, 200, 20, "brown"
draw_rect 550, 300, 180, 20, "gray"
```

**What it teaches:** velocity + gravity (`vy = vy + 1`), landing detection with `collide_rect`, a grounded flag (`on_ground`), and nested `if` for "jump only when grounded".

### 🖼️ Moving Sprites — `examples/sprite.vibes`

> ⚠️ Requires `player.png` in the working directory (unlike the three zero-asset games above).

Load a texture once, then move it around with input every frame:

```vibes
window "Sprite Demo", 800, 600

sprite player = "player.png"   # loads once, before the loop
x = 400
y = 300

if key_right { x = x + 5 }

draw_sprite player, x, y       # drawn every frame at (x, y)
```

Generated C:

```c
Texture2D player = LoadTexture("player.png");   // before the loop
while (!WindowShouldClose()) {
    if (IsKeyDown(KEY_RIGHT)) { x = (x + 5); }  // update first

    BeginDrawing();
    ClearBackground(BLACK);
    DrawTexture(player, x, y, WHITE);           // then draw
    EndDrawing();
}
UnloadTexture(player);                          // cleanup
```

Combine this with `collide_rect` and `play_sound` and you have a full game loop — the same pattern the three games above use, just with textures instead of shapes.

### More examples in `examples/`

| File | What it shows |
|---|---|
| `game.vibes` | Arrow-key movement of a circle |
| `mouse.vibes` | WASD + Space/Enter + mouse tracking |
| `sprite.vibes` | `sprite` / `draw_sprite` |
| `sound.vibes` | `load_sound`, `play_sound`, `play_music` |
| `app.vibes` | General-purpose mode: functions, print, if/else |

---

# 📚 Reference — Every Keyword & Built-in

## Keywords

| Keyword | Meaning | Example |
|---|---|---|
| `window` | Open a Raylib window (game mode) | `window "Pong", 800, 600` |
| `on` | Event block (`on update { ... }`) | `on update { x = x + 1 }` |
| `circle` / `draw_circle` | Draw a filled circle | `circle x, y, 30, "red"` |
| `draw_rect` | Draw a filled rectangle | `draw_rect x, y, 40, 40, "lime"` |
| `draw_sprite` | Draw a loaded sprite | `draw_sprite player, x, y` |
| `sprite` | Load a texture | `sprite player = "player.png"` |
| `key_right` / `key_left` / `key_up` / `key_down` | Arrow key checks | `if key_right { ... }` |
| `if` / `else` | Conditionals | `if x > 5 { ... } else { ... }` |
| `while` | Loop | `while x < 10 { x = x + 1 }` |
| `for` | C-style loop | `for i = 0, i < 10, i = i + 1 { }` |
| `break` | Exit a loop | `if done == 1 { break }` |
| `continue` | Skip to next iteration | `continue` |
| `fn` | Declare a function | `fn add(a, b) { return a + b }` |
| `return` | Return from a function | `return result` |
| `print` | Print to stdout | `print "Hello, ", name` |
| `import` | Include another `.vibes` file | `import "math.vibes"` |
| `struct` | Declare a struct | `struct Point { x, y }` |

## Built-in Functions

### Math & Random (`<math.h>` / `<stdlib.h>`)

| Function | Generated C |
|---|---|
| `sqrt(x)` | `sqrt(x)` |
| `sin(x)` / `cos(x)` / `tan(x)` | `sin(x)` / `cos(x)` / `tan(x)` |
| `floor(x)` / `ceil(x)` / `round(x)` | `floor(x)` / `ceil(x)` / `round(x)` |
| `abs(x)` | `fabs(x)` for doubles, `abs(x)` for ints |
| `min(a, b)` / `max(a, b)` | `((a) < (b) ? (a) : (b))` / `((a) > (b) ? (a) : (b))` |
| `clamp(v, lo, hi)` | ternary chain |
| `random()` | `((double)rand() / RAND_MAX)` |
| `random_int(lo, hi)` | `lo + rand() % (hi - lo + 1)` |

### Type Conversion

| Function | Generated C |
|---|---|
| `to_int(x)` | `(int)(x)` |
| `to_double(x)` | `(double)(x)` |
| `to_string(x)` | `snprintf` to a stack buffer |

### Time & Sleep

| Function | Generated C |
|---|---|
| `sleep(ms)` | `usleep(ms * 1000)` |
| `time()` | `((double)clock() / CLOCKS_PER_SEC)` |

### Input & File I/O

| Function | Generated C |
|---|---|
| `input("prompt")` | `printf` prompt + `fgets` into a string |
| `read_file("path")` | `fopen` / `fread` / `malloc` / `fclose` → `char *` |
| `file_exists("path")` | `access(path, F_OK) == 0` |
| `write_file("path", "data")` | `fopen(path, "w")` + `fputs` + `fclose` |
| `append_file("path", "data")` | `fopen(path, "a")` + `fputs` + `fclose` |

### Sound & Music (Raylib)

| Function | Generated C |
|---|---|
| `load_sound("path")` | `Sound name = LoadSound("path");` |
| `play_sound(name)` | `PlaySound(name);` |
| `play_music("path")` | `InitAudioDevice(); Music name = LoadMusicStream(...); PlayMusicStream(...); UpdateMusicStream(...);` |

### Collision (Raylib)

| Function | Generated C |
|---|---|
| `collide_circle(x1, y1, r1, x2, y2, r2)` | `CheckCollisionCircles(...)` |
| `collide_rect(x1, y1, w1, h1, x2, y2, w2, h2)` | `CheckCollisionRecs(...)` |
| `collide_point(x, y, rx, ry, rw, rh)` | `CheckCollisionPointRec(...)` |

### String Methods

| Method | Generated C |
|---|---|
| `s.len` | `strlen(s)` |
| `s.contains(sub)` | `strstr(s, sub) != NULL` |
| `s.split(delim)` | `str_split(s, delim)` helper |
| `s.replace(old, new)` | `str_replace(s, old, new)` helper |
| `s.upper()` / `s.lower()` | `str_upper(s)` / `str_lower(s)` helper |
| `s.trim()` | `str_trim(s)` helper |
| `"a" + "b"` (concatenation) | `string_concat("a", "b")` helper |

### Dictionaries

| Construct | Generated C |
|---|---|
| `d = {"alice": 95, "bob": 87}` | `dict_t d; dict_set(&d, "alice", 95); ...` |
| `d["key"]` | `dict_get(d, "key")` |
| `dict_contains(d, "key")` | helper returns 1/0 |

### Arrays

| Construct | Generated C |
|---|---|
| `numbers = [1, 2, 3]` | `int numbers[] = {1, 2, 3};` |
| `numbers[0]` | `numbers[0]` |

---

## 💡 Tips & Tricks

| Tip | Explanation |
|---|---|
| **Preview before running** | `mylang emit file.vibes` shows the exact C before you run anything. |
| **Trace a bug with --explain** | `mylang --explain emit file.vibes` shows which source line produced each C line. |
| **Order doesn't matter** | Codegen sorts code: declarations → update → draw. Put statements anywhere. |
| **Game logic goes in `on update`** | Movement, collisions, and input all belong there — they run before drawing each frame. |
| **No text rendering in games** | There's no `DrawText` built-in, and `print` only works in general-purpose (no-`window`) programs — game-mode statements are routed to the draw loop, so console text isn't emitted. Use sprite-based UI for on-screen text for now. |
| **Comments** | `#` starts a comment. |
| **Avoid `else` inside game loops** | `if` blocks in `on update` work great; keep `else` for general-purpose mode. |

---

## 🧩 VS Code Extension

Syntax highlighting for `.vibes` files lives in `vibes-vscode/`:

```bash
cp -r vibes-vscode ~/.vscode/extensions/vibes-vscode-0.1.0/
```

This colorizes keywords (`window`, `circle`, `if`), strings, numbers, and operators.

---

<div align="center">

**Happy vibing!** ✨

</div>
