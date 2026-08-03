# 🚀 Getting Started with MyLang

> **New here? Start with this file.** It explains what MyLang is, how this
> repository is organized, how the compiler works, and how to write your first
> programs — no prior experience required. It only takes about 10 minutes.

---

## 📖 Table of Contents

1. [What is MyLang?](#-what-is-mylang)
2. [How the compiler works (plain English)](#-how-the-compiler-works-plain-english)
3. [Repository tour](#-repository-tour)
4. [Installation](#-installation)
5. [Your first program](#-your-first-program)
6. [The language guide](#-the-language-guide)
7. [Writing games](#-writing-games)
8. [Common errors (and how to fix them)](#-common-errors-and-how-to-fix-them)
9. [Where to go next](#-where-to-go-next)

---

## 💡 What is MyLang?

MyLang is a **programming language you can actually use** — and it's built right
here in this repository, in plain C.

Here's the key idea:

> You write simple, readable `.vibes` files.
> MyLang **transpiles** them into real C99 code.
> That C is then compiled with `clang` and run.

There's no interpreter, no virtual machine, no magic. `mylang` is a program
that reads text and writes C. That's it.

**MyLang has two personalities:**

| 🕹️ Game mode | 📦 General purpose |
|---|---|
| `window "Pong", 800, 600` | `fn add(a, b):`<br>&nbsp;&nbsp;&nbsp;&nbsp;`return a + b` |
| `circle x, y, 30, "red"` | `print "Hello, ", name` |
| `if key_right:`<br>&nbsp;&nbsp;&nbsp;&nbsp;`x = x + 5` | `if x > 10:`<br>&nbsp;&nbsp;&nbsp;&nbsp;`...`<br>`else:`<br>&nbsp;&nbsp;&nbsp;&nbsp;`...` |
| `sprite player = "player.png"` | `scores = {"alice": 95}` |
| Uses **Raylib** for graphics | Uses plain C + stdlib |

You can write a game, a calculator, a file tool, or a text adventure — all in
the same language.

---

## ⚙️ How the compiler works (plain English)

When you run `mylang run mygame.vibes`, four things happen, one after another:

```
┌─────────────┐    ┌─────────────┐    ┌─────────────┐    ┌─────────────┐
│ 1. LEXER    │ →  │ 2. PARSER   │ →  │ 3. CODEGEN  │ →  │ 4. COMPILER │
│ reads text  │    │ builds tree │    │ writes C    │    │ clang makes │
│ into tokens │    │ (the AST)   │    │ from the    │    │ an executable│
│             │    │             │    │ AST         │    │ + runs it   │
└─────────────┘    └─────────────┘    └─────────────┘    └─────────────┘
```

Each step is a real file in this repo:

1. **Lexer** — `src/lexer.c`
   Splits your source code into *tokens* (little labeled pieces): keywords,
   numbers, strings, identifiers, punctuation.
   `x = 400` becomes: `IDENTIFIER("x")`, `EQUALS`, `NUMBER("400")`.

2. **Parser** — `src/parser.c`
   Takes the token stream and builds an **Abstract Syntax Tree (AST)** — a
   tree-shaped structure that captures the meaning of the program.
   `x = 400` becomes an "assignment" node with a variable name and a number.

3. **Codegen** — `src/codegen.c`
   Walks the AST and **prints real C code**. This is where the magic becomes
   visible — `circle x, y, 30, "red"` becomes
   `DrawCircle(x, y, 30, RED);`.

4. **CLI** — `src/main.c`
   Ties it all together: reads your file, runs the three stages, invokes
   `clang -lraylib`, and runs the resulting binary.

> **Want to *see* this happening?** Run:
> ```bash
> mylang debug examples/game.vibes       # tokens + AST tree
> mylang visualize examples/game.vibes   # the pipeline, step by step
> mylang emit examples/game.vibes        # the generated C code
> ```

---

## 📂 Repository tour

Here's what every folder and file in this repo is for:

```
mylang/
├── src/                    The compiler itself (all in C99)
│   ├── lexer.c/h           Stage 1 — text → tokens
│   ├── parser.c/h          Stage 2 — tokens → AST
│   ├── ast.h               The AST node definitions
│   ├── codegen.c/h         Stage 3 — AST → C code
│   ├── pipeline.c/h        The `mylang visualize` diagram tool
│   └── main.c              The `mylang` command-line program
│
├── examples/               Ready-to-run .vibes programs
│   ├── app.vibes           General purpose: input, arrays, file I/O
│   ├── game.vibes          Simplest game: move a ball with arrows
│   ├── pong.vibes          🏓 Full Pong game
│   ├── breakout.vibes      🧱 Brick breaker
│   └── platformer.vibes    🏃 Platformer with gravity + jumping
│
├── tests/                  C test suites (run with `make test`)
│   ├── test_lexer.c        Tests for the lexer
│   ├── test_parser.c       Tests for the parser
│   └── test_codegen.c      Tests for code generation
│
├── packages/               Source for the bundled `std` package
├── vibes-vscode/           VS Code syntax-highlighting extension
├── docs/                   docs/demo.gif — an animated showcase
├── Makefile                Build & test commands
├── README.md               The landing page (start here)
├── LEARN.md                The full language documentation
├── GETTING_STARTED.md      👈 You are here
├── CHANGELOG.md            Version history
├── CONTRIBUTING.md         How to contribute to the compiler itself
└── LICENSE                 MIT license
```

---

## 🔧 Installation

### Prerequisites

| Tool | Why |
|---|---|
| `cc` / `clang` | Compiles the generated C |
| `make` | Builds the `mylang` binary |
| [Raylib](https://www.raylib.com/) | Only needed for **game mode** (`brew install raylib`) |

### Build it

```bash
make              # builds ./mylang (should print zero warnings)
make test         # optional: runs all 110 automated tests
sudo make install # optional: puts `mylang` on your PATH
```

### Verify it works

```bash
mylang --version
```

You should see `mylang 0.1.0`.

---

## 🖐️ Your first program

Create a file called `hello.vibes`:

```vibes
# This is a comment. Comments start with #.
print "Hello, world!"
```

Run it:

```bash
mylang run hello.vibes
```

Output:

```
Hello, world!
```

**What just happened?** `print` in MyLang became `printf("Hello, world!\n");`
in C, which `clang` compiled into a real executable and ran.

Let's make it interactive — save this as `greet.vibes`:

```vibes
name = input("What's your name? ")
print "Nice to meet you, ", name
```

```bash
mylang run greet.vibes
```

```
What's your name? Ada
Nice to meet you, Ada
```

---

## 📘 The language guide

### Variables

You don't need to declare types — MyLang infers them:

```vibes
age = 25          # becomes int age = 25;
pi = 3.14         # becomes double pi = 3.14;
name = "Ada"      # becomes const char *name = "Ada";
```

Update them anytime:

```vibes
age = age + 1
```

### Functions

```vibes
fn add(a, b):
    return a + b

result = add(2, 3)
print result      # prints 5
```

### Conditionals

```vibes
score = 85

if score > 90:
    print "Excellent!"
else if score > 70:
    print "Good job!"
else:
    print "Keep practicing!"
```

### Loops

```vibes
# while loop
i = 0
while i < 5:
    print i
    i = i + 1

# for loop
for (i = 0; i < 5; i = i + 1):
    print i
```

### Lists (arrays)

```vibes
numbers = [10, 20, 30]
print numbers[0]      # 10
numbers[1] = 99       # works too
```

### Dictionaries

```vibes
scores = {"alice": 95, "bob": 87}
print scores["alice"]     # 95
```

### Strings

```vibes
name = "MyLang"
print name.len             # 6 — length
print name.upper()         # MYLANG
print name.contains("ang") # 1 (true)
```

You can join strings with `+`:

```vibes
first = "Ada"
print "Hello, " + first    # Hello, Ada
```

### File I/O

```vibes
write_file("note.txt", "My first file!")
contents = read_file("note.txt")
print contents

if file_exists("note.txt"):
    print "It exists!"
```

### Math

```vibes
print sqrt(16)          # prints 4.000000
print min(3, 7)         # 3
print max(3, 7)         # 7
print random_int(1, 6)  # a dice roll!
```

---

## 🕹️ Writing games

Game mode uses [Raylib](https://www.raylib.com/) under the hood, but you never
touch it directly — you write MyLang.

### Your first game

```vibes
window "Movable Ball", 800, 600

x = 400
y = 300

if key_right:
    x = x + 5
if key_left:
    x = x - 5
if key_up:
    y = y - 5
if key_down:
    y = y + 5

circle x, y, 30, "red"
```

```bash
mylang run mygame.vibes
```

An 800×600 window opens with a red ball you can move with the arrow keys.

### What does the generated C look like?

Run `mylang emit examples/game.vibes` and you'll see exactly how your
`.vibes` code maps to real Raylib C:

```c
#include "raylib.h"

int main(void) {
    InitWindow(800, 600, "Movable Ball");
    SetTargetFPS(60);

    int x = 400;
    int y = 300;

    while (!WindowShouldClose()) {
        if (IsKeyDown(KEY_RIGHT)) { x = (x + 5); }
        if (IsKeyDown(KEY_LEFT))  { x = (x - 5); }
        /* ... */
        BeginDrawing();
        ClearBackground(BLACK);
        DrawCircle(x, y, 30, RED);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
```

### Drawing

```vibes
circle 400, 300, 50, "blue"          # DrawCircle(400, 300, 50, BLUE);
draw_rect 100, 100, 200, 50, "green" # DrawRectangle(100, 100, 200, 50, GREEN);
```

### Keyboard input

| MyLang | What it does |
|---|---|
| `key_right` `key_left` `key_up` `key_down` | Arrow keys |
| `key_w` `key_a` `key_s` `key_d` | WASD |
| `key_space` | Space bar |
| `key_enter` | Enter key |

```vibes
if key_space:
    print "Jump!"
```

### Mouse input

```vibes
x = mouse_x        # GetMouseX()
y = mouse_y        # GetMouseY()
if mouse_down:
    print "Left button held"
if mouse_click:
    print "Clicked!"
```

### Sprites (images)

```vibes
sprite player = "player.png"   # loads the image before the game loop
draw_sprite player, x, y       # draws it each frame
```

### Sound

```vibes
play_sound("jump.wav")         # plays a sound effect
play_music("theme.mp3")        # loops background music
```

### Collision detection

```vibes
if collide_circle(x1, y1, r1, x2, y2, r2):
    print "Circles collided!"
```

### Complete mini-game: catch the point

Save this as `catch.vibes`:

```vibes
window "Catch!", 800, 600

x = 400
y = 300
target_x = 100
target_y = 100
score = 0

if key_right:
    x = x + 5
if key_left:
    x = x - 5
if key_up:
    y = y - 5
if key_down:
    y = y + 5

if collide_circle(x, y, 20, target_x, target_y, 20):
    score = score + 1
    target_x = random_int(50, 750)
    target_y = random_int(50, 550)
    print "Score: ", score    # note: score prints to the terminal

circle x, y, 20, "white"
circle target_x, target_y, 20, "red"
```

Move the white circle with the arrow keys and touch the red dot to score!

> More complete games live in `examples/` — `pong.vibes`, `breakout.vibes`,
> and `platformer.vibes` are full, playable games you can read and modify.
>
> **Tip:** from the repo root you can run them by bare name — `mylang run
> breakout.vibes` automatically finds `examples/breakout.vibes` for you.

---

## ⚠️ Common errors (and how to fix them)

MyLang reports errors with the **line and column** where the problem is:

```
Error [7:0]: expected expression, got ""
parse failed — aborting
```

This means: on **line 7**, the parser expected an expression (a number,
variable, string, etc.) but found something else.

| Symptom | Likely cause | Fix |
|---|---|---|
| `expected expression` | A missing value — e.g. `x =` with nothing after it | Give it a value: `x = 400` |
| `expected X, got Y` | Wrong syntax — a typo, or a block header missing its `:` | Check the line the error points to |
| `parse failed` | Any syntax error — the parser stops safely | Fix the first error and re-run |
| Game compiles but window never opens | Raylib not installed | `brew install raylib` |
| `Undefined symbols` at compile time | Raylib library missing in the link step | Reinstall Raylib, then re-run |

**Tip:** `mylang --explain tokens file.vibes` explains what every token in
your program means in plain English — great for debugging.

---

## 🧭 Where to go next

- **📖 [LEARN.md](LEARN.md)** — the complete language documentation with a
  step-by-step teaching path (lexer → parser → codegen) and every keyword and
  built-in function.
- **🎮 The example games** — read `examples/pong.vibes` or
  `examples/breakout.vibes` and try changing numbers to see what happens.
- **💻 VS Code extension** — get syntax highlighting: see
  `vibes-vscode/` for install instructions.
- **🛠️ Contribute** — want to add a feature to the language itself?
  Check out [CONTRIBUTING.md](CONTRIBUTING.md).
- **📦 Packages** — `mylang install std` then `import "std/math"` for ready-made
  math helpers.

---

**Happy coding!** 🎉 If something doesn't work, remember:
`mylang debug`, `mylang --explain`, and `mylang visualize` are your friends —
they show you exactly what MyLang is doing with your code.
