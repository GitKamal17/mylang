# MyLang / Vibes for VS Code

Write `.vibes` programs and games with syntax highlighting, four-space indentation,
snippets, and compiler commands in VS Code.

## Install and run

1. Install a C compiler and Make. On macOS: `xcode-select --install`.
   On Ubuntu/WSL: `sudo apt install build-essential`.
2. Clone [MyLang](https://github.com/GitKamal17/mylang) and run `make` in its root.
3. Install the `.vsix` using **Extensions → … → Install from VSIX**.
4. Open the repository folder and `examples/hello.vibes`.
5. Click the play button, or run **MyLang: Run File** from the Command Palette.

The extension detects `mylang` in the workspace root, then checks parent directories
for a MyLang source checkout, then tries PATH. An explicit `vibes.compilerPath`
always takes priority.
For other projects, install the compiler (`make install PREFIX="$HOME/.local"`
and add `$HOME/.local/bin` to PATH), or set `vibes.compilerPath` to its absolute path.
Relative compiler paths resolve from the workspace folder. Do not include arguments.

The compiler is a separate prerequisite; the VSIX does not bundle it.
macOS and Linux are supported by the compiler. On Windows, use a WSL workspace,
build the compiler inside WSL, and install this extension in WSL.
Browser-only VS Code is not supported.

## Commands

| Command | Effect |
| --- | --- |
| MyLang: Run File | Compile and run; terminal supports interactive input |
| MyLang: Build File | Create a binary alongside the source |
| MyLang: Check File | Validate parsing and code generation without compiling |
| MyLang: Show Generated C in Terminal | Show the generated C |

Run shortcut: **Ctrl+Alt+R**, or **Cmd+Alt+R** on macOS.
Commands save the active file first. Execution uses the source directory, so relative
asset and file-I/O paths resolve beside your `.vibes` file. Compiler errors appear in
the task terminal. Check does not type-check generated C or validate assets.

Graphics require [Raylib](https://www.raylib.com/) separately (`brew install raylib`
on macOS). Plain programs do not need Raylib.

Snippets: `fn`, `ifelse`, `while`, `window`, `print`.
Highlighting and snippets work in Restricted Mode; compiler commands require trust.
There is no debugger, language server, or live error underlining yet.

## Develop and package

From the repository root, press F5 to open an Extension Development Host.
Then open a `.vibes` file in that window.

```sh
cd vibes-vscode
npm ci
npm test
npm run package
```

Install `vibes-vscode-0.2.1.vsix` through VS Code's **Install from VSIX** command.
Marketplace publication requires ownership of the publisher ID in `package.json`;
packaging does not publish the extension.
