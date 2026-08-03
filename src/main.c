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

#include <ctype.h>      /* isxdigit */
#include <errno.h>      /* errno, EINTR */
#include <stdint.h>     /* uint8_t, uint32_t */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>    /* strcasecmp */
#include <unistd.h>     /* fork, close */
#include <sys/types.h>  /* pid_t */
#include <sys/stat.h>   /* mkdir, stat */
#include <sys/wait.h>   /* waitpid, WIFEXITED, WIFSIGNALED */
#include <dirent.h>     /* opendir, readdir */

/* Version string — injected by the Makefile (-DMYLANG_VERSION="...").
   Defaults to "dev" when built by hand. */
#ifndef MYLANG_VERSION
#define MYLANG_VERSION "dev"
#endif

/* read_file is defined below; declared early because cmd_install uses it. */
static char *read_file(const char *path, long *out_len);

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
    printf("  install <pkg>    Install a package (std, an HTTPS URL, or github:owner/repo/main/file.vibes)\n");
    printf("                   Append #sha256=<hex> to a URL to verify its checksum\n");
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

/* ------------------------------------------------------------------ *
 * Process execution — fork + execvp, never the shell.                *
 *                                                                     *
 * Arguments are passed verbatim, so filenames or URLs containing       *
 * shell metacharacters (quotes, backticks, $(...)) can never be        *
 * interpreted by a shell. This is the fix for the old system() sites.  *
 * ------------------------------------------------------------------ */
static int run_argv(char *const argv[])
{
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return -1; }

    if (pid == 0) {
        execvp(argv[0], argv);
        fprintf(stderr, "error: cannot execute '%s'\n", argv[0]);
        _exit(127);
    }

    int status;
    for (;;) {
        if (waitpid(pid, &status, 0) < 0) {
            if (errno == EINTR) continue;   /* e.g. SIGINT to the group */
            perror("waitpid");
            return -1;
        }
        break;
    }

    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) {
        /* Low 7 bits of the wait status carry the signal number (POSIX).
           WIFTERMSIG is hidden under -std=c99 on macOS, so we extract it
           manually for portability. */
        int sig = status & 0x7f;
        fprintf(stderr, "error: '%s' terminated by signal %d\n", argv[0], sig);
        return 128 + sig;
    }
    return 1;
}

/* ------------------------------------------------------------------ *
 * SHA-256 — self-contained implementation used to verify the          *
 * checksum of downloaded packages.                                    *
 * ------------------------------------------------------------------ */
static const uint32_t SHA256_K[64] = {
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
    0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
    0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
    0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
    0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
    0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
};

#define SHA256_ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha256_transform(uint32_t s[8], const uint8_t block[64])
{
    uint32_t w[64];
    uint32_t a, b, c, d, e, f, g, h;
    int i;

    for (i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i * 4] << 24)
             | ((uint32_t)block[i * 4 + 1] << 16)
             | ((uint32_t)block[i * 4 + 2] << 8)
             |  (uint32_t)block[i * 4 + 3];
    }
    for (i = 16; i < 64; i++) {
        uint32_t s0 = SHA256_ROTR(w[i - 15], 7) ^ SHA256_ROTR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = SHA256_ROTR(w[i - 2], 17) ^ SHA256_ROTR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    a = s[0]; b = s[1]; c = s[2]; d = s[3];
    e = s[4]; f = s[5]; g = s[6]; h = s[7];

    for (i = 0; i < 64; i++) {
        uint32_t S1 = SHA256_ROTR(e, 6) ^ SHA256_ROTR(e, 11) ^ SHA256_ROTR(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t S0 = SHA256_ROTR(a, 2) ^ SHA256_ROTR(a, 13) ^ SHA256_ROTR(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + maj;
        uint32_t t1 = h + S1 + ch + SHA256_K[i] + w[i];
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    s[0] += a; s[1] += b; s[2] += c; s[3] += d;
    s[4] += e; s[5] += f; s[6] += g; s[7] += h;
}

static void sha256(const uint8_t *msg, size_t len, uint8_t out[32])
{
    uint32_t s[8] = {
        0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
        0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u
    };
    uint64_t bitlen = (uint64_t)len * 8u;
    uint8_t block[64];
    size_t i;

    /* Process all whole 64-byte blocks */
    size_t full = len & ~(size_t)63u;
    for (i = 0; i < full; i += 64)
        sha256_transform(s, msg + i);

    /* Final padded block(s): append 0x80, pad to 56 mod 64, add bit length */
    size_t rem = len - full;
    memcpy(block, msg + full, rem);
    block[rem] = 0x80;
    if (rem < 56) {
        memset(block + rem + 1, 0, 56 - (rem + 1));
        for (i = 0; i < 8; i++)
            block[56 + i] = (uint8_t)(bitlen >> (56 - 8 * i));
        sha256_transform(s, block);
    } else {
        memset(block + rem + 1, 0, 64 - (rem + 1));
        sha256_transform(s, block);
        memset(block, 0, 56);
        for (i = 0; i < 8; i++)
            block[56 + i] = (uint8_t)(bitlen >> (56 - 8 * i));
        sha256_transform(s, block);
    }

    for (i = 0; i < 8; i++) {
        out[i * 4]     = (uint8_t)(s[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(s[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(s[i] >> 8);
        out[i * 4 + 3] = (uint8_t)(s[i]);
    }
}

static void sha256_hex(const uint8_t digest[32], char out[65])
{
    static const char hx[] = "0123456789abcdef";
    int i;
    for (i = 0; i < 32; i++) {
        out[i * 2]     = hx[digest[i] >> 4];
        out[i * 2 + 1] = hx[digest[i] & 0x0f];
    }
    out[64] = '\0';
}

/* True if s is exactly 64 lowercase/uppercase hex digits */
static int is_hex64(const char *s)
{
    size_t i;
    if (strlen(s) != 64) return 0;
    for (i = 0; i < 64; i++)
        if (!isxdigit((unsigned char)s[i])) return 0;
    return 1;
}

/* mylang install <pkg>
   Supports:
     std                                        - bundled standard package
     https://host/path/file.vibes               - HTTPS package URL
     https://host/path/file.vibes#sha256=<hex>  - pin + verify the SHA-256
     github:owner/repo/main/path/file.vibes     - GitHub raw shorthand

   Plaintext http:// URLs are refused: installed packages are code that gets
   compiled and executed, so they must arrive over TLS (or be pinned to a
   checksum the user provides). */
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

    /* Strip an optional #sha256=<hex> fragment before URL handling */
    char pkgbuf[1024];
    int plen = snprintf(pkgbuf, sizeof pkgbuf, "%s", pkg);
    if (plen < 0 || (size_t)plen >= sizeof pkgbuf) {
        fprintf(stderr, "error: package URL too long\n");
        return 1;
    }
    const char *expected_sha = NULL;
    char *frag = strchr(pkgbuf, '#');
    if (frag) {
        if (strncmp(frag, "#sha256=", 8) != 0) {
            fprintf(stderr, "error: unsupported URL fragment '%s' (use #sha256=<64 hex chars>)\n", frag);
            return 1;
        }
        expected_sha = frag + 8;
        if (!is_hex64(expected_sha)) {
            fprintf(stderr, "error: malformed sha256 checksum (expected 64 hex chars)\n");
            return 1;
        }
        *frag = '\0';
    }

    /* Refuse plaintext HTTP: downloaded packages become executable code. */
    if (strncmp(pkgbuf, "http://", 7) == 0) {
        fprintf(stderr, "error: refusing insecure plaintext 'http://' package URL\n");
        fprintf(stderr, "  packages are code that gets compiled and executed; use https://\n");
        fprintf(stderr, "  or pin a checksum: https://.../math.vibes#sha256=<64 hex chars>\n");
        return 1;
    }

    /* Remote package: build the download URL */
    char url[2048];
    const char *fname;
    if (strncmp(pkgbuf, "https://", 8) == 0) {
        snprintf(url, sizeof url, "%s", pkgbuf);
        const char *slash = strrchr(url, '/');
        fname = slash ? slash + 1 : url;
    } else if (strncmp(pkgbuf, "github:", 7) == 0) {
        /* github:owner/repo/main/path/file.vibes -> raw.githubusercontent.com */
        snprintf(url, sizeof url, "https://raw.githubusercontent.com/%s", pkgbuf + 7);
        const char *slash = strrchr(url, '/');
        fname = slash ? slash + 1 : url;
    } else {
        fprintf(stderr, "error: unknown package '%s'\n", pkg);
        fprintf(stderr, "  usage: mylang install std\n");
        fprintf(stderr, "         mylang install https://example.com/math.vibes\n");
        fprintf(stderr, "         mylang install https://example.com/math.vibes#sha256=<hex>\n");
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

    /* Download with curl via exec — no shell, so no command injection.
       Download to a temp file and rename into place only on success, so a
       failed or interrupted download never leaves a partial package. */
    char tmp_dest[1300];
    snprintf(tmp_dest, sizeof tmp_dest, "%s.tmp", dest);
    char *const dl[] = { "curl", "-fsSL", url, "-o", tmp_dest, NULL };
    int rc = run_argv(dl);
    if (rc != 0) {
        remove(tmp_dest);
        fprintf(stderr, "error: download failed for '%s' (is curl installed?)\n", url);
        return 1;
    }

    /* Verify the pinned SHA-256 checksum, if one was supplied */
    if (expected_sha) {
        long len = 0;
        char *data = read_file(tmp_dest, &len);
        if (!data) { remove(tmp_dest); return 1; }
        uint8_t digest[32];
        char hex[65];
        sha256((const uint8_t *)data, (size_t)len, digest);
        sha256_hex(digest, hex);
        free(data);
        if (strcasecmp(hex, expected_sha) != 0) {
            fprintf(stderr, "error: checksum mismatch for '%s'\n", url);
            fprintf(stderr, "  expected  sha256:%s\n", expected_sha);
            fprintf(stderr, "  actual    sha256:%s\n", hex);
            remove(tmp_dest);
            return 1;
        }
        printf("  sha256 verified: %s\n", hex);
    } else {
        printf("  warning: no #sha256=<hex> checksum pinned — content is unverified\n");
    }

    /* Move the verified download into place (atomic on the same filesystem) */
    if (rename(tmp_dest, dest) != 0) {
        fprintf(stderr, "error: cannot move package into place: %s\n", dest);
        remove(tmp_dest);
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
    if (size < 0) { fclose(fp); return NULL; }
    /* fseek (not rewind) so failures are caught instead of relying on errno */
    if (fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); return NULL; }

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

    /* Resolve raylib's include/lib dirs instead of hardcoding /opt/homebrew:
       Apple-Silicon Homebrew, Intel Homebrew, then plain system paths. */
    const char *raylib_root = NULL;
    if (needs_raylib) {
        static const char *candidates[] = { "/opt/homebrew", "/usr/local", NULL };
        for (int i = 0; candidates[i]; ++i) {
            char probe[512];
            snprintf(probe, sizeof probe, "%s/include/raylib.h", candidates[i]);
            struct stat st;
            if (stat(probe, &st) == 0) { raylib_root = candidates[i]; break; }
        }
    }

    /* Build the argv array — exec'ed directly, never through a shell. */
    char inc_arg[512], lib_arg[512];
    char *argv[16];
    int n = 0;
    argv[n++] = "clang";
    argv[n++] = "-std=c99";
    argv[n++] = "-Wall";
    argv[n++] = "-Wextra";
    if (raylib_root) {
        snprintf(inc_arg, sizeof inc_arg, "-I%s/include", raylib_root);
        snprintf(lib_arg, sizeof lib_arg, "-L%s/lib", raylib_root);
        argv[n++] = inc_arg;
        argv[n++] = lib_arg;
    }
    argv[n++] = "-o";
    argv[n++] = (char *)bin_path;
    argv[n++] = (char *)c_path;
    if (needs_raylib) argv[n++] = "-lraylib";
    argv[n++] = "-lm";
    argv[n++] = NULL;

    /* Display the equivalent command (diagnostics only — argv is what runs) */
    fprintf(stderr, "  clang");
    for (int i = 1; argv[i]; ++i) fprintf(stderr, " %s", argv[i]);
    fprintf(stderr, "\n");

    int ret = run_argv(argv);
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

    /* exec'ed directly — safe for any bin_path, no shell involved */
    char *const argv[] = { (char *)bin_path, NULL };
    int ret = run_argv(argv);

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
    /* Unique, unguessable temp path (mkstemp) — the old /tmp/mylang_<pid>
       was predictable and raceable via a symlink (TOCTOU). */
    char tmpl[] = "/tmp/mylang_XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0) { fprintf(stderr, "error: cannot create temporary file\n"); return 1; }
    close(fd);

    int ret = cmd_build(source_path, tmpl);
    if (ret != 0) { remove(tmpl); return ret; }

    ret = run_binary(tmpl);
    remove(tmpl);
    return ret;
}

/* ------------------------------------------------------------------ *
 * Resolve a source file path before opening it.                     *
 *                                                                     *
 * The given path is used verbatim when it exists. A bare filename     *
 * (no '/') that isn't found in the current folder is also looked for  *
 * in the repo's examples/ directory, so `mylang run breakout.vibes`   *
 * works from the repo root. Explicit paths (with a '/') are never     *
 * rewritten, so error messages stay accurate.                         *
 * ------------------------------------------------------------------ */
static void resolve_source_path(const char *path, char *out, size_t out_size)
{
    struct stat st;
    if (stat(path, &st) == 0) {
        snprintf(out, out_size, "%s", path);
        return;
    }

    if (strchr(path, '/') == NULL) {
        snprintf(out, out_size, "examples/%s", path);
        if (stat(out, &st) == 0) {
            fprintf(stderr, "note: '%s' not found in the current folder — using '%s'\n",
                    path, out);
            return;
        }
        /* The fallback failed too — say where we looked. */
        fprintf(stderr, "note: '%s' not found in the current folder or in examples/\n",
                path);
    }

    snprintf(out, out_size, "%s", path);
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
        char resolved[1100];
        resolve_source_path(argv[3], resolved, sizeof resolved);
        if (strcmp(argv[2], "emit") == 0)
            return cmd_explain_emit(resolved);
        else if (strcmp(argv[2], "tokens") == 0)
            return cmd_explain_tokens(resolved);
        else {
            fprintf(stderr, "error: unknown --explain subcommand '%s'\n", argv[2]);
            return 1;
        }
    }

    const char *cmd  = argv[1];
    char resolved[1100];
    resolve_source_path(argv[2], resolved, sizeof resolved);
    const char *file = resolved;

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
