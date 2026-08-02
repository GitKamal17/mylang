# 🔒 MyLang — Security Audit

**Scope:** the entire repository (`src/`, `tests/`, `scripts/`, `Makefile`,
`.github/workflows/ci.yml`, `packages/`, `examples/`, docs).
**Date:** 2026-08-02 · **Method:** manual source review + targeted grep scans.
**Mode:** Originally delivered as diagnosis-only. All findings have since been
fixed — see [✅ Fixes applied](#-fixes-applied-2026-08-02) at the end.

**Legend:** 🟢 not found · 🔴 found · ⚪️ not applicable to this project

---

## ⚖️ Executive summary

**Verdict: the project is not a security failure.** MyLang is a single-user,
local CLI transpiler. It has **no database, no web server, no HTTP API, no
users, no auth, no sessions, no cookies, no HTML/JS output, and no
third-party runtime dependencies**. That means the bulk of the classic
web-security checklist is genuinely **not applicable** — this report says so
honestly rather than inventing problems.

The **real** findings are all in one place — the shell command construction in
`src/main.c` — plus a few hygiene gaps (no static analysis, plain-HTTP
package downloads, predictable temp paths). Details with file:line below.

| Category | Verdict |
|---|---|
| 1. SQL injection (3) | ⚪️ all — no database exists |
| 2. Authentication (3) | ⚪️ all — no auth system exists |
| 3. Data leakage (3) | 🔴 1 · 🟢 2 |
| 4. Access control (3) | ⚪️ all — single-user CLI |
| 5. XSS (2) | ⚪️ all — generates C, not HTML/JS |
| 6. Insecure deserialization (3) | ⚪️ all — no serialization formats; related risk noted |
| 7. Vulnerable dependencies (3) | 🟢 2 · 🔴 1 (no static analysis) |
| 8. Logging & monitoring (2) | 🟢 1 · ⚪️ 1 |

**Count:** 🔴 **4** (1 low + 3 hygiene) · 🟢 **9** · ⚪️ **9**

---

## 1️⃣ SQL Injection — ⚪️ NOT APPLICABLE

The project contains **no SQL and no database of any kind**. A repo-wide scan
found zero references to SQL, ORM, database drivers, connection strings, or
query builders. There is nothing to inject into, so all three items are
marked not applicable rather than "passed."

- **1.1 String concatenation in queries** — ⚪️ No queries exist.
- **1.2 Missing input validation** — ⚪️ No database input path exists. *(Note:
  the closest analogue — unsanitized strings being fed to `system()` — IS a
  real finding; see General Hardening **G1**.)*
- **1.3 Raw ORM queries without parameter binding** — ⚪️ No ORM exists.

## 2️⃣ Authentication — ⚪️ NOT APPLICABLE

There are no users, no login, no password storage, no session management, and
no rate limiting anywhere in the project. `input()` in the language just reads
a line from stdin for the user's own programs.

- **2.1 Passwords in plaintext / weak hashes** — ⚪️ No password handling exists.
- **2.2 No rate limiting on login attempts** — ⚪️ No login exists.
- **2.3 Predictable session tokens** — ⚪️ No sessions exist.

## 3️⃣ Data Leakage — 🔴 1 · 🟢 2

### 3.1 HTTP instead of HTTPS — 🔴 FOUND (low severity)

`mylang install` **explicitly accepts and downloads over plaintext HTTP**:

- `src/main.c:124` — `strncmp(pkg, "http://", 7)` accepts raw `http://` URLs
- `src/main.c:154-155` — downloads via `curl -fsSL "<url>"` with **no TLS
  enforcement, no warning, and no checksum/signature verification**

A network attacker can MITM a `mylang install http://...` and substitute a
malicious `.vibes` file, which is then compiled and **executed** (see 6.1 and
**G3**). Low severity because it requires the user to opt into an `http://`
URL, but the tool should at least refuse or loudly warn.

**Where to dig:** `src/main.c:104-160` (`cmd_install`), package-fetch path.

### 3.2 Secrets in code instead of a secret manager — 🟢 NOT FOUND

Scanned `.c`, `.h`, `.vibes`, `.md`, `.yml`, `.sh` for API keys, tokens,
passwords, and private-key material. **None found** — no credentials exist in
the repository. (A secret manager would be overkill for a dependency-free CLI
tool.)

### 3.3 Sensitive data in logs — 🟢 NOT FOUND

The tool logs only paths, token dumps, ASTs, and compiler output to
`stderr`, and nothing is persisted or sent anywhere. No file *contents* are
logged by the compiler itself. ⚠️ Note: running `mylang debug` or
`mylang step` on a `.vibes` file that itself contains secrets will echo that
source to `stderr` — that is the tool working as designed, not a leak.

## 4️⃣ Access Control — ⚪️ NOT APPLICABLE

Single-user, local, single-process tool. There are no roles, no objects owned
by different users, no IDs to tamper with, and no login-time vs. request-time
authorization distinction to make.

- **4.1 Frontend-only permission checks** — ⚪️ No frontend exists.
- **4.2 IDOR (accessing others' objects by ID)** — ⚪️ No multi-user objects exist.
- **4.3 Role checked once at login, not per request** — ⚪️ No roles exist.

*(The only privilege boundary is the filesystem: `install` writes into
`~/.mylang/packages` with `0755` dirs — see `src/main.c:52-75`.)*

## 5️⃣ XSS — ⚪️ NOT APPLICABLE

- **5.1 Unsafe innerHTML** — ⚪️ The tool outputs **C source code**, never HTML
  or JavaScript; there is no DOM to attack.
- **5.2 Trusting URL/cookie data without sanitization** — ⚪️ No runtime URL or
  cookie data exists. *(The command-line *URL* handling does relate to **G1** —
  see General Hardening.)*

## 6️⃣ Insecure Deserialization — ⚪️ NOT APPLICABLE (with a note)

No serialization formats are used anywhere — no pickle, no Java
Serialization, no JSON/YAML parsing, no `readObject`. Parsing is a hand-written
C99 lexer/parser that validates input and reports `line:column` errors.

- **6.1 Deserialization from an untrusted source** — ⚪️ No deserialization
  exists. ⚠️ **Closest analogue — real risk:** `mylang install` fetches
  **arbitrary remote code** and `import` compiles it into your program
  (`src/main.c:104-160`, parser import resolution in `src/parser.c`). There is
  no signature, checksum, or publisher trust. This is a **supply-chain** issue,
  not deserialization — detailed in **G3**.
- **6.2 No class whitelist** — ⚪️ No deserialization exists.
- **6.3 Unsafe formats (Java Serializable, pickle)** — ⚪️ None used.

## 7️⃣ Vulnerable Dependencies — 🟢 2 · 🔴 1

### 7.1 Outdated libraries — 🟢 NOT FOUND

The project has **zero third-party dependencies** — pure C99 + libc/libm, with
optional Raylib for game mode. There are no `requirements.txt`,
`package.json`, or vendored libraries to be outdated. *(Portability note: the
Raylib flags hardcode Homebrew paths `-I/opt/homebrew/include
-L/opt/homebrew/lib` at `src/main.c:254-258` — breaks on Intel Macs/Linux, but
that's a build bug, not a security issue.)*

### 7.2 Abandoned packages — 🟢 NOT FOUND

No third-party packages are depended on. The bundled `std` package lives
in-repo (`packages/std/math.vibes`) and is also embedded in the binary
(`src/main.c:93-103`).

### 7.3 No static analysis — 🔴 FOUND

- `Makefile` builds with `-Wall -Wextra` only — **no `-fanalyzer`, no ASan,
  no UBSan, no clang-tidy, no scan-build, no fuzzing**.
- `.github/workflows/ci.yml` runs build → tests → warnings-check → a couple of
  `emit` smoke tests. Nothing memory- or fuzz-oriented.
- The lexer/parser are the **primary untrusted-input surface** (anyone can
  feed them any file) and get zero sanitizer/fuzzer coverage.

**Where to dig:** `Makefile` (add a `sanitize`/`analyze` target),
`.github/workflows/ci.yml` (add an ASan+UBSan job and a fuzz smoke test over
`tests/errors_multi.vibes`-style inputs).

## 8️⃣ Logging & Monitoring — 🟢 1 · ⚪️ 1

### 8.1 Critical events not logged — 🟢 NOT FOUND

Every failure path writes to `stderr` **and** returns a non-zero exit code:
parse errors include `line:column`, compile errors surface the full `clang`
output, and build/run status is printed. For a CLI tool this is adequate
logging.

### 8.2 No alerting on anomalies — ⚪️ NOT APPLICABLE

There is no daemon, server, or long-running process — nothing to monitor or
alert on. A local CLI cannot meaningfully "alert."

---

## 🛠 General Hardening Notes (beyond the 22-item checklist)

These are the findings that actually matter for this codebase. **Nothing below
was changed — pointers only.**

### 🔴 G1 — OS command injection via `system()` (highest-priority finding)

Three `system()` call sites build shell commands from **unchecked** strings,
mostly `argv`-derived:

| Site | Line | Data flowing into the shell |
|---|---|---|
| `cmd_install` | `src/main.c:154-155` | `url` and `dest` from the install argument (arbitrary) |
| `compile` | `src/main.c:254,262,275` | `bin_path` derived from the **input filename** (`src/main.c:431-447`), `c_path` = `bin_path` + `".c"` |
| `run_binary` | `src/main.c:292-293` | `bin_path` (same filename-derived value) |

The double quotes do **not** protect anything: a filename or URL containing
`"`, backticks, or `$(...)` breaks out of the quoted string and injects shell
commands. E.g. `mylang build 'x"$(touch /tmp/pwned)"'` would execute the
payload. (The test suites at `tests/test_parser.c:430,457` and
`tests/test_codegen.c:763,792` only run `mkdir`/`rm` on PID-named /tmp dirs —
benign.)

**Where to dig:** replace `system()` with `fork`/`execvp` argv-style
invocation, or reject any filename/URL containing shell metacharacters.

### 🔴 G2 — Predictable temp path (TOCTOU / symlink race)

`src/main.c:687` — `run` writes and executes `/tmp/mylang_<pid>`. PIDs are
guessable; a local attacker could pre-create a symlink at that path between
the write and the `system()` call.

**Where to dig:** use `mkstemp()` / `O_EXCL` with a random suffix, or build in
a user-private directory (`$XDG_RUNTIME_DIR` or `~/.cache/mylang`).

### 🔴 G3 — Supply-chain / integrity gap in `mylang install`

`src/main.c:104-160`: downloads packages over **plain HTTP** (allowed at
`src/main.c:124`), with **no checksum, no signature, no pinning**, and the
result is compiled and run as part of the user's program. This is
dependency-confusion / MITM / malicious-package exposure.

**Where to dig:** require `https://`, warn (or refuse) on `http://`, verify a
pinned SHA-256, and warn before compiling code imported from remote packages.

### 🟢 G4 — Everything else is clean

- No `strcpy`/`strcat`/`sprintf` in `src/` — the string builder in `codegen.c`
  grows dynamically; the few fixed buffers are `snprintf`-bounded and
  `strncpy`-bounded with explicit NUL-termination (`src/parser.c:652`,
  `src/pipeline.c:132,368`).
- No credentials, tokens, or private keys anywhere in the repo.
- Test isolation is decent (temp dirs under `/tmp/<pid>`, cleaned up).

---

## 📎 Appendix — "`mylang debug examples/game.vibes` doesn't work" diagnosis

**The project's logic is fine — this is a stale-install / PATH problem.**

| Binary | Version | `debug` command |
|---|---|---|
| `/usr/local/bin/mylang` (installed **Jul 29**, root-owned) | older build | ❌ `error: unknown command 'debug'` |
| `./mylang` (fresh build from this repo, **Aug 2**) | `mylang 0.1.0` | ✅ works — tokens + AST print correctly |

When you type `mylang`, your shell finds `/usr/local/bin/mylang` first — an
**outdated copy installed 4 days ago** that predates the `debug`/`step`/
`visualize`/`--explain` commands. The source in this repo has them and they
run perfectly:

```text
$ ./mylang debug examples/game.vibes
=== TOKENS ===
  [1:1] KEYWORD      "window"
  [1:9] STRING       "Movable Ball"
  ... (67 tokens)
=== AST ===
Program (8 statements): WINDOW / ASSIGN(x) / ASSIGN(y) / IF(key_right) / ...
```

**Fix (one of):**
```bash
sudo make install        # replace the stale /usr/local/bin/mylang
# or just use the repo build:
./mylang debug examples/game.vibes
```

---

---

## ✅ Fixes applied (2026-08-02)

Every finding in this report has been fixed. Summary:

| Finding | Fix |
|---|---|
| 🔴 **G1** — OS command injection via `system()` | `src/main.c` — all three `system()` sites replaced with `fork`/`execvp` (`run_argv`). No shell is ever involved, so filenames/URLs are passed verbatim (quotes, backticks, `$(...)` are inert). |
| 🔴 **G2** — predictable `/tmp/mylang_<pid>` | `src/main.c` (`cmd_run`) — now uses `mkstemp()` for a unique, unguessable temp path. |
| 🔴 **G3** / 🔴 3.1 — supply chain (`http://` + no integrity) | `src/main.c` (`cmd_install`) — plaintext `http://` is refused; optional `#sha256=<hex>` pinning is verified on download (self-contained SHA-256); a warning is printed when no checksum is pinned. |
| 🔴 **7.3** — no static analysis | `Makefile` — new `make sanitize` (ASan+UBSan over tests and real transpilations) and `make analyze` (clang static analyzer); both wired into `.github/workflows/ci.yml`, plus CI regression checks for command injection and checksum verification. |
| 🟢 **7.1** note — hardcoded `/opt/homebrew` | `src/main.c` (`compile`) — raylib include/lib dirs are now auto-detected (`/opt/homebrew` → `/usr/local` → system paths). |

*Report generated 2026-08-02. Diagnosis first; fixes applied 2026-08-02.*
