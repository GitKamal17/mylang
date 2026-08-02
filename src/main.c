/**
 * main.c — CLI entry point for the MyLang transpiler.
 *
 * Usage:
 *   mylang emit   <file.vibes>   # Print generated C to stdout
 *   mylang build  <file.vibes>   # Compile to a binary (no run)
 *   mylang run    <file.vibes>   # Compile and run immediately
 *   mylang --version             # Print version (from the VERSION file)
 */

#include "codegen.h"
#include "lexer.h"
#include "parser.h"
#include "pipeline.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>  /* getpid */
#include <sys/stat.h>   /* mkdir, stat */
#include <dirent.h>     /* opendir, readdir */

/* Version string — injected by the Makefile (-DMYLANG_VERSION="...").
   Defaults to "dev" when built by hand. */
#ifndef MYLANG_VERSION
#define MYLANG_VERSION "dev"
#endif

/* ------------------------------------------------------------------ *
 * Help / usage                                                       *
 * ------------------------------------------------------------------ */
static void print_usage(void)
{
    printf("Usage: mylang <command> <file.vibes>\n\n");
    printf("Commands:\n");
    printf("  emit   <file>    Parse and print generated C to stdout\n");
    printf("  build  <file>    Parse, generate C, and compile to binary\n");
    printf("  run    <file>    Parse, generate C, compile, and run\n");
    printf("  debug  <file>    Print tokens and AST tree to stderr\n");
    printf("  step   <file>    Step-through: tokens, then AST, then generated C\n");
    printf("  visualize <file>  Show the lexer → parser → codegen pipeline diagram\n");
    printf("  --explain emit   <file>    Show annotated C code with source-line comments\n");
    printf("  --explain tokens <file>    Show each token with plain-English explanations\n");
    printf("  --version                  Print the mylang version and exit\n");
    printf("  install <pkg>    Install a package (std, a URL, or github:owner/repo/main/file.vibes)\n");
    printf("  list             List installed packages (~/.mylang/packages)\n");
}

/* ------------------------------------------------------------------ *
 * Package manager                                                    *
 * ------------------------------------------------------------------ */

/* ~/.mylang/packages (fall back to ./.mylang/packages if HOME is unset) */
static void get_packages_dir(char *buf, size_t size)
{
    const char *home = getenv("HOME");
    if (!home || !home[0]) home = ".";
    snprintf(buf, size, "%s/.mylang/packages", home);
}

/* mkdir -p: create all missing path components */
static void mkdir_p(const char *path)
{
    char tmp[1024];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; ++p) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

/* Write a string to a file; returns 0 on success */
static int write_text_file(const char *path, const char *content)
{
    FILE *fp = fopen(path, "w");
    if (!fp) { fprintf(stderr, "error: cannot write '%s'\n", path); return 1; }
    fputs(content, fp);
    fclose(fp);
    return 0;
}

/* Bundled std package (mirrors packages/std/math.vibes in the repo) */
static const char *STD_MATH_VIBES =
    "# std/math - standard math helpers\n"
    "\n"
    "fn distance(x1, y1, x2, y2) {\n"
    "    return sqrt((x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1))\n"
    "}\n"
    "\n"
    "fn lerp(a, b, t) {\n"
    "    return a + (b - a) * t\n"
    "}\n";

/* mylang install <pkg>
   Supports:
     std                                        - bundled standard package
     https://host/path/file.vibes               - any HTTP(S) URL
     github:owner/repo/main/path/file.vibes     - GitHub raw shorthand */
static int cmd_install(const char *pkg)
{
    char pkgs[1024];
    get_packages_dir(pkgs, sizeof pkgs);
    mkdir_p(pkgs);

    /* Bundled std package */
    if (strcmp(pkg, "std") == 0) {
        char dir[1100], path[1200];
        snprintf(dir, sizeof dir, "%s/std", pkgs);
        mkdir_p(dir);
        snprintf(path, sizeof path, "%s/math.vibes", dir);
        if (write_text_file(path, STD_MATH_VIBES) != 0) return 1;
        printf("installed std -> %s\n", path);
        return 0;
    }

    /* Remote package: build the download URL */
    char url[2048];
    const char *fname;
    if (strncmp(pkg, "http://", 7) == 0 || strncmp(pkg, "https://", 8) == 0) {
        snprintf(url, sizeof url, "%s", pkg);
        const char *slash = strrchr(url, '/');
        fname = slash ? slash + 1 : url;
    } else if (strncmp(pkg, "github:", 7) == 0) {
        /* github:owner/repo/main/path/file.vibes -> raw.githubusercontent.com */
        snprintf(url, sizeof url, "https://raw.githubusercontent.com/%s", pkg + 7);
        const char *slash = strrchr(url, '/');
        fname = slash ? slash + 1 : url;
    } else {
        fprintf(stderr, "error: unknown package '%s'\n", pkg);
        fprintf(stderr, "  usage: mylang install std\n");
        fprintf(stderr, "         mylang install https://example.com/math.vibes\n");
        fprintf(stderr, "         mylang install github:owner/repo/main/math.vibes\n");
        return 1;
    }

    /* Package name = file basename without .vibes */
    char name[256];
    snprintf(name, sizeof name, "%s", fname);
    size_t nl = strlen(name);
    if (nl > 6 && strcmp(name + nl - 6, ".vibes") == 0)
        name[nl - 6] = '\0';

    char dir[1100], dest[1200];
    snprintf(dir, sizeof dir, "%s/%s", pkgs, name);
    mkdir_p(dir);
    snprintf(dest, sizeof dest, "%s/%s", dir, fname);

    char cmd[2600];
    snprintf(cmd, sizeof cmd, "curl -fsSL \"%s\" -o \"%s\"", url, dest);
    int rc = system(cmd);
    if (rc != 0) {
        fprintf(stderr, "error: download failed for '%s' (is curl installed?)\n", url);
        return 1;
    }
    printf("installed %s -> %s\n", name, dest);
    return 0;
}

/* mylang list: show installed packages */
static int cmd_list(void)
{
    char pkgs[1024];
    get_packages_dir(pkgs, sizeof pkgs);

    DIR *dir = opendir(pkgs);
    if (!dir) {
        printf("No packages installed yet (run 'mylang install std').\n");
        return 0;
    }

    printf("Installed packages in %s:\n", pkgs);
    int found = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        char sub[1100];
        snprintf(sub, sizeof sub, "%s/%s", pkgs, ent->d_name);
        struct stat st;
        if (stat(sub, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            printf("  %s/\n", ent->d_name);
            /* list .vibes files inside the package dir */
            DIR *subdir = opendir(sub);
            if (subdir) {
                struct dirent *fe;
                while ((fe = readdir(subdir)) != NULL) {
                    size_t fl = strlen(fe->d_name);
                    if (fl > 6 && strcmp(fe->d_name + fl - 6, ".vibes") == 0)
                        printf("      %s\n", fe->d_name);
                }
                closedir(subdir);
            }
            found = 1;
        }
    }
    closedir(dir);
    if (!found) printf("  (none)\n");
    return 0;
}

/* ------------------------------------------------------------------ *
 * Read entire file into a heap-allocated buffer.                     *
 * ------------------------------------------------------------------ */
static char *read_file(const char *path, long *out_len)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) { fprintf(stderr, "error: cannot open '%s'\n", path); return NULL; }

    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return NULL; }
    long size = ftell(fp);
    rewind(fp);

    char *buf = (char *)malloc((size_t)size + 1);
    if (!buf) { fprintf(stderr, "error: out of memory\n"); fclose(fp); return NULL; }

    size_t nread = fread(buf, 1, (size_t)size, fp);
    fclose(fp);

    if ((long)nread != size) { fprintf(stderr, "error: failed to read '%s'\n", path); free(buf); return NULL; }
    buf[size] = '\0';
    if (out_len) *out_len = size;
    return buf;
}

/* ------------------------------------------------------------------ *
 * Write the generated C source to a file.                            *
 * ------------------------------------------------------------------ */
static int write_c_file(const char *path, const char *source)
{
    FILE *fp = fopen(path, "w");
    if (!fp) { fprintf(stderr, "error: cannot write '%s'\n", path); return 1; }
    size_t len = strlen(source);
    if (fwrite(source, 1, len, fp) != len) { fprintf(stderr, "error: short write\n"); fclose(fp); return 1; }
    fclose(fp);
    return 0;
}

/* ------------------------------------------------------------------ *
 * Compile a .c file into a binary with clang.                        *
 * ------------------------------------------------------------------ */
static int compile(const char *c_path, const char *bin_path, int needs_raylib)
{
    fprintf(stderr, "compiling...\n");

    char cmd[2048];
    int n;

    if (needs_raylib) {
        n = snprintf(cmd, sizeof cmd,
                     "clang -std=c99 -Wall -Wextra "
                     "-I/opt/homebrew/include "
                     "-L/opt/homebrew/lib "
                     "-lraylib -lm "
                     "-o \"%s\" \"%s\" 2>&1",
                     bin_path, c_path);
    } else {
        n = snprintf(cmd, sizeof cmd,
                     "clang -std=c99 -Wall -Wextra "
                     "-lm "
                     "-o \"%s\" \"%s\" 2>&1",
                     bin_path, c_path);
    }

    if (n < 0 || (size_t)n >= sizeof cmd) {
        fprintf(stderr, "error: command too long\n");
        return 1;
    }

    fprintf(stderr, "  %s\n", cmd);
    int ret = system(cmd);

    if (ret != 0)
        fprintf(stderr, "compilation failed (exit code %d)\n", ret);

    return ret;
}

/* ------------------------------------------------------------------ *
 * Run a binary, wait for it to finish.                               *
 * ------------------------------------------------------------------ */
static int run_binary(const char *bin_path)
{
    fprintf(stderr, "running...\n\n");
    fflush(stderr);

    char cmd[1024];
    snprintf(cmd, sizeof cmd, "\"%s\"", bin_path);
    int ret = system(cmd);

    fprintf(stderr, "\n(exit code %d)\n", ret);
    return ret;
}

/* ------------------------------------------------------------------ *
 * Extract the directory part from a file path.                       *
 * Writes into out (up to out_size bytes).                            *
 * If no directory separator, writes ".".                             *
 * ------------------------------------------------------------------ */
static void get_base_dir(const char *path, char *out, size_t out_size)
{
    const char *slash = strrchr(path, '/');
    if (slash) {
        size_t len = (size_t)(slash - path);
        if (len >= out_size) len = out_size - 1;
        memcpy(out, path, len);
        out[len] = '\0';
    } else {
        out[0] = '.';
        out[1] = '\0';
    }
}

/* ------------------------------------------------------------------ *
 * Main pipeline: read -> lex -> parse -> codegen.                    *
 * Returns the generated C string on success, NULL on error.          *
 * Also outputs the needs_raylib flag.                                *
 * ------------------------------------------------------------------ */
static char *transpile(const char *source_path, int *out_needs_raylib)
{
    long src_len = 0;
    char *source = read_file(source_path, &src_len);
    if (!source) return NULL;

    char base_dir[1024];
    get_base_dir(source_path, base_dir, sizeof(base_dir));

    lexer_t lexer;
    lexer_init(&lexer, source);

    parser_t parser;
    parser_init(&parser, &lexer, base_dir);
    program_t *prog = parse_program(&parser);

    if (parser.had_error) {
        fprintf(stderr, "parse failed — aborting\n");
        parser_free_imports(&parser);
        ast_destroy_program(prog);
        free(source);
        return NULL;
    }

    codegen_result_t cr = codegen_emit(prog);
    parser_free_imports(&parser);
    ast_destroy_program(prog);
    free(source);

    if (!cr.code) {
        fprintf(stderr, "codegen failed\n");
        return NULL;
    }

    if (out_needs_raylib)
        *out_needs_raylib = cr.needs_raylib;

    return cr.code;
}

/* ------------------------------------------------------------------ *
 * Subcommand: emit                                                   *
 * ------------------------------------------------------------------ */
static int cmd_emit(const char *source_path)
{
    int needs_raylib = 0;
    char *c_code = transpile(source_path, &needs_raylib);
    if (!c_code) return 1;

    printf("%s", c_code);
    free(c_code);
    return 0;
}

/* ------------------------------------------------------------------ *
 * Subcommand: build                                                  *
 * ------------------------------------------------------------------ */
static int cmd_build(const char *source_path, const char *bin_path)
{
    int needs_raylib = 0;
    char *c_code = transpile(source_path, &needs_raylib);
    if (!c_code) return 1;

    size_t bin_len = strlen(bin_path);
    char *c_path = (char *)malloc(bin_len + 4);
    if (!c_path) { fprintf(stderr, "OOM\n"); free(c_code); return 1; }
    snprintf(c_path, bin_len + 4, "%s.c", bin_path);

    if (write_c_file(c_path, c_code) != 0) { free(c_code); free(c_path); return 1; }
    free(c_code);

    int ret = compile(c_path, bin_path, needs_raylib);
    remove(c_path);
    free(c_path);
    return ret;
}

/* ------------------------------------------------------------------ *
 * --explain tokens: print each token with a plain-English explanation*
 * ------------------------------------------------------------------ */
static int cmd_explain_tokens(const char *source_path)
{
    long src_len = 0;
    char *source = read_file(source_path, &src_len);
    if (!source) return 1;

    lexer_t lexer;
    lexer_init(&lexer, source);

    printf("Token Explanations for: %s\n", source_path);
    printf("%-8s %-12s %-20s  %s\n", "[L:C]", "TYPE", "TEXT", "EXPLANATION");
    printf("%s\n", "-------- ------------ --------------------  -------------------------------------");

    for (;;) {
        token_t tok = lexer_next(&lexer);
        if (tok.type == TOKEN_EOF) {
            printf("%-8s %-12s %-20s  %s\n", "[EOF]", "EOF", "--", token_type_explain(tok.type));
            break;
        }

        char loc[16];
        snprintf(loc, sizeof loc, "[%d:%d]", tok.line, tok.column);

        char text[22];
        if (tok.type == TOKEN_NEWLINE) {
            snprintf(text, sizeof text, "\\n");
        } else if (tok.type == TOKEN_ERROR) {
            snprintf(text, sizeof text, "%.*s", (int)tok.length, tok.start);
        } else {
            size_t maxw = (tok.length < 18) ? tok.length : 18;
            memcpy(text, tok.start, maxw);
            text[maxw] = '\0';
        }

        printf("%-8s %-12s %-20s  %s\n",
               loc, token_type_name(tok.type), text,
               token_type_explain(tok.type));

        if (tok.type == TOKEN_ERROR) break;
    }

    free(source);
    return 0;
}

/* ------------------------------------------------------------------ *
 * --explain emit: show annotated C code with source-line markers     *
 * ------------------------------------------------------------------ */
static int cmd_explain_emit(const char *source_path)
{
    long src_len = 0;
    char *source = read_file(source_path, &src_len);
    if (!source) return 1;

    char base_dir[1024];
    get_base_dir(source_path, base_dir, sizeof(base_dir));

    lexer_t lexer;
    lexer_init(&lexer, source);

    parser_t parser;
    parser_init(&parser, &lexer, base_dir);
    program_t *prog = parse_program(&parser);

    if (parser.had_error) {
        fprintf(stderr, "parse failed — aborting\n");
        parser_free_imports(&parser);
        ast_destroy_program(prog);
        free(source);
        return 1;
    }

    /* Extract just the filename from the path (strip directory) */
    const char *fname = strrchr(source_path, '/');
    fname = fname ? fname + 1 : source_path;

    codegen_result_t cr = codegen_emit_explain(prog, fname);
    parser_free_imports(&parser);
    ast_destroy_program(prog);
    free(source);

    if (!cr.code) {
        fprintf(stderr, "codegen failed\n");
        return 1;
    }

    printf("%s", cr.code);
    free(cr.code);
    return 0;
}

/* ------------------------------------------------------------------ *
 * Subcommand: visualize                                              *
 * ------------------------------------------------------------------ */
static int cmd_visualize(const char *source_path)
{
    long src_len = 0;
    char *source = read_file(source_path, &src_len);
    if (!source) return 1;

    const char *fname = strrchr(source_path, '/');
    fname = fname ? fname + 1 : source_path;

    pipeline_visualize(source, fname);

    free(source);
    return 0;
}

/* ------------------------------------------------------------------ *
 * Subcommand: debug                                                  *
 * ------------------------------------------------------------------ */
static int cmd_debug(const char *source_path)
{
    long src_len = 0;
    char *source = read_file(source_path, &src_len);
    if (!source) return 1;

    char base_dir[1024];
    get_base_dir(source_path, base_dir, sizeof(base_dir));

    /* ---- Lex and print tokens ---- */
    lexer_t lexer;
    lexer_init(&lexer, source);

    fprintf(stderr, "=== TOKENS ===\n");
    int tok_count = 0;
    for (;;) {
        token_t tok = lexer_next(&lexer);
        fprintf(stderr, "  [%d:%d] %-12s ",
                tok.line, tok.column, token_type_name(tok.type));
        if (tok.type == TOKEN_EOF) {
            fprintf(stderr, "(end)\n");
            break;
        } else if (tok.type == TOKEN_ERROR) {
            fprintf(stderr, "%.*s\n", (int)tok.length, tok.start);
            break;
        } else if (tok.type == TOKEN_NEWLINE) {
            fprintf(stderr, "\\n\n");
        } else {
            fprintf(stderr, "\"%.*s\"\n", (int)tok.length, tok.start);
        }
        tok_count++;
    }
    fprintf(stderr, "  (%d tokens)\n\n", tok_count);

    /* ---- Re-init lexer and parse ---- */
    lexer_init(&lexer, source);

    parser_t parser;
    parser_init(&parser, &lexer, base_dir);
    program_t *prog = parse_program(&parser);

    if (parser.had_error) {
        fprintf(stderr, "=== PARSE FAILED ===\n");
        parser_free_imports(&parser);
        ast_destroy_program(prog);
        free(source);
        return 1;
    }

    /* ---- Print AST ---- */
    fprintf(stderr, "=== AST ===\n");
    ast_debug_print(prog);

    parser_free_imports(&parser);
    ast_destroy_program(prog);
    free(source);
    return 0;
}

/* ------------------------------------------------------------------ *
 * Step-through debugger: press Enter to advance through              *
 *   1. tokens   2. AST nodes   3. generated C lines                  *
 * All output goes to stderr; stdout stays clean.                    *
 * ------------------------------------------------------------------ */
static void step_pause(void)
{
    fprintf(stderr, "  [Press Enter to continue...] ");
    fflush(stderr);
    int c;
    do {
        c = getchar();
    } while (c != '\n' && c != EOF);
    fputc('\n', stderr);
}

static int cmd_step(const char *source_path)
{
    long src_len = 0;
    char *source = read_file(source_path, &src_len);
    if (!source) return 1;

    char base_dir[1024];
    get_base_dir(source_path, base_dir, sizeof(base_dir));

    /* ---- Step 1: tokens ---- */
    lexer_t lexer;
    lexer_init(&lexer, source);

    fprintf(stderr, "=== STEP 1: TOKENS ===\n");
    int tok_count = 0;
    for (;;) {
        token_t tok = lexer_next(&lexer);
        fprintf(stderr, "  [%d:%d] %-12s ",
                tok.line, tok.column, token_type_name(tok.type));
        if (tok.type == TOKEN_EOF) {
            fprintf(stderr, "(end of file)\n");
            break;
        } else if (tok.type == TOKEN_ERROR) {
            fprintf(stderr, "\"%.*s\"  <-- LEX ERROR\n", (int)tok.length, tok.start);
            break;
        } else if (tok.type == TOKEN_NEWLINE) {
            fprintf(stderr, "\\n\n");
        } else {
            fprintf(stderr, "\"%.*s\"\n", (int)tok.length, tok.start);
        }
        tok_count++;
        step_pause();
    }
    fprintf(stderr, "  (%d tokens)\n", tok_count);

    /* ---- Step 2: AST ---- */
    lexer_init(&lexer, source);

    parser_t parser;
    parser_init(&parser, &lexer, base_dir);
    program_t *prog = parse_program(&parser);

    if (parser.had_error) {
        fprintf(stderr, "=== PARSE FAILED ===\n");
        parser_free_imports(&parser);
        ast_destroy_program(prog);
        free(source);
        return 1;
    }

    fprintf(stderr, "\n=== STEP 2: AST ===\n");
    fprintf(stderr, "Program (%zu statements):\n", prog->count);
    for (size_t i = 0; i < prog->count; ++i) {
        fprintf(stderr, "  --- statement %zu/%zu ---\n", i + 1, prog->count);
        ast_debug_print_stmt(prog->stmts[i]);
        step_pause();
    }

    /* ---- Step 3: generated C ---- */
    codegen_result_t cr = codegen_emit(prog);

    if (!cr.code) {
        fprintf(stderr, "codegen failed\n");
        parser_free_imports(&parser);
        ast_destroy_program(prog);
        free(source);
        return 1;
    }

    fprintf(stderr, "\n=== STEP 3: GENERATED C ===\n");
    const char *p = cr.code;
    int lineno = 1;
    while (*p) {
        const char *nl = strchr(p, '\n');
        if (!nl) {
            fprintf(stderr, "%4d | %s\n", lineno++, p);
            break;
        }
        fprintf(stderr, "%4d | %.*s\n", lineno++, (int)(nl - p), p);
        p = nl + 1;
        step_pause();
    }

    free(cr.code);
    parser_free_imports(&parser);
    ast_destroy_program(prog);
    free(source);
    return 0;
}

/* ------------------------------------------------------------------ *
 * Subcommand: run                                                    *
 * ------------------------------------------------------------------ */
static int cmd_run(const char *source_path)
{
    pid_t pid = getpid();
    char bin_path[256];
    snprintf(bin_path, sizeof bin_path, "/tmp/mylang_%d", (int)pid);

    int ret = cmd_build(source_path, bin_path);
    if (ret != 0) { remove(bin_path); return ret; }

    ret = run_binary(bin_path);
    remove(bin_path);
    return ret;
}

/* ------------------------------------------------------------------ *
 * Entry point                                                        *
 * ------------------------------------------------------------------ */
int main(int argc, char **argv)
{
    /* Package manager commands take fewer args and come first */
    if (argc >= 2 && strcmp(argv[1], "list") == 0)
        return cmd_list();
    if (argc >= 2 && strcmp(argv[1], "install") == 0) {
        if (argc < 3) {
            fprintf(stderr, "error: install requires a package name\n\n");
            print_usage();
            return 1;
        }
        return cmd_install(argv[2]);
    }

    /* Version flag — needs no other arguments */
    if (argc >= 2 && (strcmp(argv[1], "--version") == 0 ||
                      strcmp(argv[1], "-v") == 0 ||
                      strcmp(argv[1], "version") == 0)) {
        printf("mylang %s\n", MYLANG_VERSION);
        return 0;
    }

    if (argc < 3) {
        print_usage();
        return (argc == 2 && strcmp(argv[1], "--help") == 0) ? 0 : 1;
    }

    /* Handle --explain prefix flag */
    if (strcmp(argv[1], "--explain") == 0) {
        if (argc < 4) {
            fprintf(stderr, "error: --explain requires a subcommand and file\n");
            return 1;
        }
        if (strcmp(argv[2], "emit") == 0)
            return cmd_explain_emit(argv[3]);
        else if (strcmp(argv[2], "tokens") == 0)
            return cmd_explain_tokens(argv[3]);
        else {
            fprintf(stderr, "error: unknown --explain subcommand '%s'\n", argv[2]);
            return 1;
        }
    }

    const char *cmd  = argv[1];
    const char *file = argv[2];

    if (strcmp(cmd, "emit") == 0) {
        return cmd_emit(file);
    } else if (strcmp(cmd, "build") == 0) {
        size_t flen = strlen(file);
        char *bin_name = (char *)malloc(flen + 8);
        if (!bin_name) { fprintf(stderr, "OOM\n"); return 1; }

        const char *dot = strrchr(file, '.');
        if (dot && strcmp(dot, ".vibes") == 0) {
            size_t base_len = (size_t)(dot - file);
            memcpy(bin_name, file, base_len);
            bin_name[base_len] = '\0';
        } else {
            snprintf(bin_name, flen + 8, "%s.out", file);
        }

        int ret = cmd_build(file, bin_name);
        if (ret == 0) fprintf(stderr, "built: %s\n", bin_name);
        free(bin_name);
        return ret;
    } else if (strcmp(cmd, "run") == 0) {
        return cmd_run(file);
    } else if (strcmp(cmd, "debug") == 0) {
        return cmd_debug(file);
    } else if (strcmp(cmd, "step") == 0) {
        return cmd_step(file);
    } else if (strcmp(cmd, "visualize") == 0) {
        return cmd_visualize(file);
    } else {
        fprintf(stderr, "error: unknown command '%s'\n\n", cmd);
        print_usage();
        return 1;
    }
}
