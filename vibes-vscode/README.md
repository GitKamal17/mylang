# MyLang — Vibes for VS Code

Create simple programs and games in `.vibes`, then click Run.
The extension includes the MyLang compiler: no repository clone, Make, compiler
PATH setup, or separate MyLang installation is needed.

## Start a project

1. Install the extension for your operating system and CPU. Before Marketplace
   publication, use **Extensions → … → Install from VSIX** with the matching package.
2. Open the Command Palette and choose **MyLang: Create Project**.
3. Choose **Hello World** or **Movable Ball**, select a parent folder, and name
   your new project. Existing folders are never overwritten.
4. Choose **Open Project**, open `main.vibes`, and click the play button.
5. If a dependency is missing, follow the setup message and retry.

## Required tools

The MyLang compiler is bundled, but generated C still needs a C toolchain.

| Environment | Programs | Games additionally need |
| --- | --- | --- |
| macOS 14+, Apple Silicon or Intel | `xcode-select --install` | `brew install raylib` (requires Homebrew) |
| Linux / WSL, x64 or ARM64, glibc 2.35+ | Clang and development headers; Ubuntu/Debian: `sudo apt install clang build-essential` | [Raylib development headers and library](https://github.com/raysan5/raylib/wiki/Working-on-GNU-Linux) |

Use **MyLang: Check Setup** to test either programs or games. Run/Build also check
the required tools automatically, including Raylib linking for game programs.
Detailed compiler errors and installation advice appear in **Output → MyLang Setup**.
No system tools are installed automatically. Native Windows, Alpine/musl, and
browser-only VS Code are not supported. Windows users should open a WSL folder
and install the Linux extension in that WSL environment.

## Commands

| Command | Effect |
| --- | --- |
| Create Project | Create a Hello World program or movable-ball game |
| Check Setup | Check the compiler, Clang/system headers, and optionally Raylib |
| Run File | Save, compile, and run with interactive terminal input |
| Build File | Save and create a binary beside the source |
| Check File | Validate parsing and code generation without Clang or Raylib |
| Show Generated C in Terminal | Print generated C without compiling |

Run shortcut: **Ctrl+Alt+R**, or **Cmd+Alt+R** on macOS.
Relative assets and file-I/O paths resolve beside the source file.
Highlighting and snippets work in Restricted Mode; compiler execution requires trust.
Snippets: `fn`, `ifelse`, `while`, `window`, `print`.
There is no debugger, language server, or live error underlining yet.

## Compiler overrides

Normally the bundled compiler is used, even if an older `mylang` exists on PATH.
Developers can set `vibes.compilerPath` to another executable (no arguments).
Relative paths resolve from the workspace folder. Source checkouts without a
bundled binary retain local compiler discovery for extension development.

## Development

```sh
make
cd vibes-vscode
npm ci
npm run package
npm test
```

Packaging builds a fresh native compiler and creates
`vibes-vscode-0.3.0-<platform>-<architecture>.vsix`.
Press F5 from the repository root for an Extension Development Host.
See the repository's `vibes-vscode/PUBLISHING.md` for platform CI and publishing.
