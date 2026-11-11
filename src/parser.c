#include "parser.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>   /* fprintf, stderr, fopen, fclose, FILE */
#include <stdlib.h>  /* malloc, realloc, free, exit, getenv */
#include <string.h>  /* strncmp, strlen, strcpy, strrchr */
#include <sys/stat.h>  /* stat, S_ISREG */

/* ------------------------------------------------------------------ *
 * File-system helpers for import resolution                            *
 * ------------------------------------------------------------------ */

/* Read an entire file into a heap-allocated buffer. Caller must free. */
static char *read_entire_file(const char *path, size_t *out_len)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;

    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return NULL; }
    long sz = ftell(fp);
    if (sz < 0) { fclose(fp); return NULL; }
    /* fseek (not rewind) so failures are caught instead of relying on errno */
    if (fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); return NULL; }

    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(fp); return NULL; }

    size_t nread = fread(buf, 1, (size_t)sz, fp);
    fclose(fp);

    if ((long)nread != sz) { free(buf); return NULL; }
    buf[sz] = '\0';
    if (out_len) *out_len = (size_t)sz;
    return buf;
}

/* Extract the directory part of a path. "dir/file.vibes" -> "dir".
   If there's no directory separator, returns ".". */
static void get_dir(const char *path, char *out, size_t out_size)
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

/* Resolve an import path relative to a base directory.
   If base_dir is "." or empty, just copies filename.
   Writes into out (up to out_size bytes). */
static void resolve_import_path(const char *base_dir, const char *filename,
                                char *out, size_t out_size)
{
    if (base_dir && base_dir[0] && strcmp(base_dir, ".") != 0) {
        if (snprintf(out, out_size, "%s/%s", base_dir, filename) >= (int)out_size)
            out[0] = '\0';
    } else {
        snprintf(out, out_size, "%s", filename);
    }
}

/* Try to resolve an import inside the user package directory
   (~/.mylang/packages). Returns 1 and writes the full path into out
   if the file exists (tries <name> then <name>.vibes). */
/* True if path names an existing regular file (not a directory). */
static int is_regular_file(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return S_ISREG(st.st_mode);
}

static int resolve_package_import(const char *filename, char *out, size_t out_size)
{
    const char *home = getenv("HOME");
    if (!home || !home[0]) return 0;

    char pkg_root[1024];
    snprintf(pkg_root, sizeof pkg_root, "%s/.mylang/packages", home);

    /* 1. As-is: ~/.mylang/packages/<filename> */
    if (snprintf(out, out_size, "%s/%s", pkg_root, filename) >= (int)out_size) return 0;
    if (is_regular_file(out)) return 1;

    /* 2. With .vibes appended: ~/.mylang/packages/<filename>.vibes
       (this is where 'mylang install std' puts std/math.vibes) */
    if (snprintf(out, out_size, "%s/%s.vibes", pkg_root, filename) >= (int)out_size) return 0;
    if (is_regular_file(out)) return 1;

    /* 3. Single-file package dir: ~/.mylang/packages/<dir>/<base>.vibes
       (this is where 'mylang install <url>' puts a one-file package,
       e.g. triple/triple.vibes, so import "triple" resolves here) */
    const char *base = strrchr(filename, '/');
    base = base ? base + 1 : filename;
    if (snprintf(out, out_size, "%s/%s/%s.vibes", pkg_root, filename, base) >= (int)out_size) return 0;
    if (is_regular_file(out)) return 1;

    return 0;
}

/* Check if a resolved path is already in the import stack (circular). */
static int in_import_stack(parser_t *p, const char *resolved)
{
    for (size_t i = 0; i < p->import_count; ++i) {
        if (strcmp(p->import_stack[i], resolved) == 0)
            return 1;
    }
    return 0;
}

/* Push a resolved path onto the import stack. */
static void push_import(parser_t *p, const char *resolved)
{
    if (p->import_count >= p->import_cap) {
        size_t nc = p->import_cap ? p->import_cap * 2 : 8;
        char **t = (char **)realloc(p->import_stack, nc * sizeof(char *));
        if (!t) { fprintf(stderr, "OOM\n"); exit(EXIT_FAILURE); }
        p->import_stack = t;
        p->import_cap   = nc;
    }
    p->import_stack[p->import_count] = strdup(resolved);
    if (!p->import_stack[p->import_count]) { fprintf(stderr, "OOM\n"); exit(EXIT_FAILURE); }
    p->import_count++;
}

/* Pop the top of the import stack and free its string. */
static void pop_import(parser_t *p)
{
    if (p->import_count > 0) {
        free(p->import_stack[p->import_count - 1]);
        p->import_stack[p->import_count - 1] = NULL;
        p->import_count--;
    }
}

/* Store a heap-allocated source buffer in the program so it stays
   alive as long as the program (and gets freed with it). */
static void store_source(program_t *prog, char *source)
{
    if (!source) return;
    if (prog->source_count >= prog->source_cap) {
        size_t nc = prog->source_cap ? prog->source_cap * 2 : 8;
        char **t = (char **)realloc(prog->sources, nc * sizeof(char *));
        if (!t) { fprintf(stderr, "OOM\n"); exit(EXIT_FAILURE); }
        prog->sources = t;
        prog->source_cap = nc;
    }
    prog->sources[prog->source_count++] = source;
}

/* ------------------------------------------------------------------ *
 * Forward declarations of internal parsing functions.                *
 * ------------------------------------------------------------------ */
static program_t *parse_program_internal(parser_t *p);
static void      synchronize(parser_t *p);
static stmt_t    *parse_declaration(parser_t *p);
static stmt_t    *parse_statement(parser_t *p);
static stmt_t    *parse_fn_stmt(parser_t *p);
static stmt_t    *parse_struct_stmt(parser_t *p);
static stmt_t    *parse_window_stmt(parser_t *p);
static stmt_t    *parse_sprite_stmt(parser_t *p);
static stmt_t    *parse_event_stmt(parser_t *p);
static stmt_t    *parse_legacy_call_stmt(parser_t *p);
static stmt_t    *parse_if_stmt(parser_t *p);
static stmt_t    *parse_while_stmt(parser_t *p);
static stmt_t    *parse_for_stmt(parser_t *p);
static stmt_t    *parse_print_stmt(parser_t *p);
static stmt_t    *parse_return_stmt(parser_t *p);
static stmt_t    *parse_break_stmt(parser_t *p);
static stmt_t    *parse_continue_stmt(parser_t *p);
static stmt_t    *parse_assign_or_expr_stmt(parser_t *p, token_t name);
static stmt_t    *parse_as_expr_stmt(parser_t *p);
static expr_t    *parse_expr(parser_t *p);
static expr_t    *parse_assignment(parser_t *p);
static expr_t    *parse_equality(parser_t *p);
static expr_t    *parse_comparison(parser_t *p);
static expr_t    *parse_addition(parser_t *p);
static expr_t    *parse_term(parser_t *p);
static expr_t    *parse_unary(parser_t *p);
static expr_t    *parse_call(parser_t *p);
static expr_t    *parse_primary(parser_t *p);
static void      expr_destroy(expr_t *e);

/* ------------------------------------------------------------------ *
 * Parser helpers                                                     *
 * ------------------------------------------------------------------ */

static void advance(parser_t *p)
{
    p->previous = p->current;
    p->current  = lexer_next(p->lexer);
    /* INDENT/DEDENT are real tokens now: block bodies are bounded by
       them (expect_block_body / close_block_body).  They only ever
       appear at logical-line boundaries, so expression parsing never
       encounters them mid-expression. */
}

static bool check(parser_t *p, token_type_t type)
{
    return p->current.type == type;
}

static bool match(parser_t *p, token_type_t type)
{
    if (!check(p, type))
        return false;
    advance(p);
    return true;
}

/* ------------------------------------------------------------------ *
 * Error reporting with source line + caret                           *
 * ------------------------------------------------------------------ */

static void print_source_line(const char *src, int target_line, int column,
                              size_t tok_length, const char *prefix)
{
    /* Find the start of the target line in the source */
    const char *line_start = src;
    int cur_line = 1;
    while (cur_line < target_line && *line_start) {
        if (*line_start == '\n') { ++cur_line; }
        ++line_start;
    }
    if (cur_line < target_line) return;

    /* Find end of the line */
    const char *line_end = line_start;
    while (*line_end && *line_end != '\n') ++line_end;

    /* Print the source line with prefix */
    fprintf(stderr, "%s%.*s\n", prefix, (int)(line_end - line_start), line_start);

    /* Print the caret line */
    fprintf(stderr, "%s", prefix);
    for (int i = 1; i < column; ++i) {
        /* Handle tabs visually */
        if (i < column && (size_t)(line_start + i - 1 - src) < (size_t)(line_start - src) + (size_t)(line_end - line_start) &&
            line_start[i - 1] == '\t')
            fprintf(stderr, "\t");
        else
            fprintf(stderr, " ");
    }
    if (tok_length == 0) tok_length = 1;
    for (size_t i = 0; i < tok_length && (int)i + column <= (int)(line_end - line_start) + 1; ++i)
        fputc('^', stderr);
    fputc('\n', stderr);
}

static void error_at(parser_t *p, const token_t *tok, const char *msg)
{
    fprintf(stderr, "Error [%d:%d]: %s\n", tok->line, tok->column, msg);
    if (tok->type != TOKEN_EOF && tok->type != TOKEN_ERROR && p->lexer->start) {
        print_source_line(p->lexer->start, tok->line, tok->column,
                          tok->length, "  ");
    }
    p->had_error = 1;
    p->panicking = 1;
}

static void error_fmt(parser_t *p, const token_t *tok, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "Error [%d:%d]: ", tok->line, tok->column);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);

    if (tok->type != TOKEN_EOF && tok->type != TOKEN_ERROR && p->lexer->start) {
        print_source_line(p->lexer->start, tok->line, tok->column,
                          tok->length, "  ");
    }
    p->had_error = 1;
    p->panicking = 1;
}

static token_t consume(parser_t *p, token_type_t type, const char *msg)
{
    if (check(p, type)) {
        advance(p);
        return p->previous;
    }

    /* Error recovery: report the missing token, then skip to a safe
       synchronization point (newline, semicolon, or a brace) so parsing
       can resume at the next statement.  If we are already recovering
       from an error in this statement, don't pile on more errors — just
       skip quietly to the next safe point. */
    if (!p->panicking) {
        if (p->current.type == TOKEN_EOF)
            error_at(p, &p->current, msg);
        else
            error_fmt(p, &p->current, "%s, got \"%.*s\"", msg,
                      (int)p->current.length, p->current.start);
    }

    synchronize(p);
    return p->current;
}

/* Skip tokens until a safe synchronization point — the start or end of
   a statement/block (newline, semicolon, or brace) or EOF.  The sync
   token itself is left for the caller (block loops exit on a closing
   brace; skip_newlines consumes newlines). */
static void synchronize(parser_t *p)
{
    while (!check(p, TOKEN_NEWLINE) && !check(p, TOKEN_SEMICOLON) &&
           !check(p, TOKEN_BRACE_OPEN) && !check(p, TOKEN_BRACE_CLOSE) &&
           !check(p, TOKEN_EOF))
        advance(p);
}

static bool token_equals(token_t t, const char *s)
{
    size_t len = strlen(s);
    return t.length == len && strncmp(t.start, s, len) == 0;
}

static char peek_raw(parser_t *p)
{
    const char *ptr = p->lexer->current;
    while (*ptr == ' ' || *ptr == '\t')
        ++ptr;
    return *ptr;
}

static void skip_newlines(parser_t *p)
{
    int crossed = 0;
    while (match(p, TOKEN_NEWLINE)) { crossed = 1; }
    /* A real statement boundary resets the in-statement error state. */
    if (crossed)
        p->panicking = 0;
}

/* ------------------------------------------------------------------ *
 * Block headers                                                      *
 * ------------------------------------------------------------------ *
 * Migration step 1: block headers are now terminated by ':' followed
 * by a newline, e.g.  "fn f(a, b):"  /  "if x > 5:"  /  "while x:".
 *
 * Legacy '{' headers are still recognised, but produce one clear error
 * telling the author to switch to ':' + newline; the brace is then
 * consumed so the block body still parses (see expect_block_body,
 * migration step 2).
 * ------------------------------------------------------------------ */
static void expect_block_header(parser_t *p, const char *what)
{
    /* New syntax: ':' followed by a newline (EOF also ends the line). */
    if (match(p, TOKEN_COLON)) {
        if (check(p, TOKEN_NEWLINE))
            advance(p);
        else if (!check(p, TOKEN_EOF))
            error_fmt(p, &p->current, "expected a newline after ':'");
        return;
    }

    /* Legacy syntax: '{' right after the header.  Report one clear
       migration error, then absorb the brace so the body still parses
       and we don't cascade unrelated errors into it. */
    if (check(p, TOKEN_BRACE_OPEN)) {
        if (!p->panicking)
            error_fmt(p, &p->current,
                      "expected ':' after %s — block headers now use ':' "
                      "followed by a newline instead of '{'", what);
        advance(p);
        return;
    }

    /* Missing colon entirely. */
    {
        char msg[128];
        snprintf(msg, sizeof msg, "expected ':' after %s", what);
        consume(p, TOKEN_COLON, msg);

        /* consume()'s synchronize() halts on newlines and braces; absorb
           a stray legacy '{' (same line or the next) so the body still
           parses and we don't cascade unrelated errors. */
        if (check(p, TOKEN_BRACE_OPEN))
            advance(p);
        if (check(p, TOKEN_NEWLINE))
            advance(p);
        if (check(p, TOKEN_BRACE_OPEN))
            advance(p);
    }
}

/* ------------------------------------------------------------------ *
 * Block bodies                                                       *
 * ------------------------------------------------------------------ *
 * Migration step 2: block bodies are now bounded by indentation —
 * TOKEN_INDENT opens the body and TOKEN_DEDENT closes it.  Legacy
 * '{' bodies are still tolerated: the header already flagged them,
 * and the closing '}' is absorbed so old code keeps parsing with just
 * those header errors.  The body arrays (ast.h/codegen.c) are
 * untouched — only how the parser bounds them changed.
 * ------------------------------------------------------------------ */

/* Open a block body: expect the INDENT marker.  (A legacy '{' header
   leaves a NEWLINE before the marker, so skip newlines first.) */
static void expect_block_body(parser_t *p)
{
    skip_newlines(p);
    consume(p, TOKEN_INDENT, "expected an indented block");
}

/* Close a block body: consume the DEDENT (new style).  Legacy bodies
   end with '}' instead, and a stray legacy '}' right after the DEDENT
   is absorbed too, so old "} else {" chains keep parsing with only
   the header migration errors. */
static void close_block_body(parser_t *p)
{
    if (match(p, TOKEN_DEDENT)) {
        if (check(p, TOKEN_BRACE_CLOSE))
            advance(p);   /* legacy '}' still closing the block */
        return;
    }
    if (check(p, TOKEN_BRACE_CLOSE)) {
        advance(p);       /* legacy '}' body terminator */
        return;
    }
    consume(p, TOKEN_DEDENT, "expected end of indented block");
}

/* ------------------------------------------------------------------ *
 * Allocators                                                         *
 * ------------------------------------------------------------------ */

static expr_t *alloc_expr(void)
{
    expr_t *e = (expr_t *)calloc(1, sizeof(expr_t));
    if (!e) { fprintf(stderr, "out of memory\n"); exit(EXIT_FAILURE); }
    return e;
}

static stmt_t *alloc_stmt(void)
{
    stmt_t *s = (stmt_t *)calloc(1, sizeof(stmt_t));
    if (!s) { fprintf(stderr, "out of memory\n"); exit(EXIT_FAILURE); }
    return s;
}

/* Dynamic array helpers */
#define GROW_ARRAY(ptr, count, cap, elem_size)                            \
    do {                                                                  \
        if ((count) >= (cap)) {                                           \
            size_t new_cap_ = (cap) ? (cap) * 2 : 8;                     \
            void *tmp_ = realloc((ptr), new_cap_ * (elem_size));          \
            if (!tmp_) { fprintf(stderr, "OOM\n"); exit(EXIT_FAILURE); }  \
            (ptr) = tmp_; (cap) = new_cap_;                               \
        }                                                                 \
    } while (0)

static void append_stmt(program_t *prog, stmt_t *s)
{
    GROW_ARRAY(prog->stmts, prog->count, prog->capacity, sizeof(stmt_t *));
    prog->stmts[prog->count++] = s;
}

/* ------------------------------------------------------------------ *
 * Number-parsing helper                                              *
 * ------------------------------------------------------------------ */

static double parse_double(const char *start, size_t length)
{
    double result   = 0.0;
    double fraction = 0.0;
    double divisor  = 1.0;
    bool   seen_dot = false;

    for (size_t i = 0; i < length; ++i) {
        char c = start[i];
        if (c == '.') {
            seen_dot = true;
        } else if (seen_dot) {
            fraction = fraction * 10.0 + (double)(c - '0');
            divisor *= 10.0;
        } else {
            result = result * 10.0 + (double)(c - '0');
        }
    }
    return result + fraction / divisor;
}

/* ------------------------------------------------------------------ *
 * AST debug printing                                                *
 * ------------------------------------------------------------------ */

static void debug_print_expr(const expr_t *e, int indent)
{
    for (int i = 0; i < indent; ++i) fprintf(stderr, "  ");
    if (!e) { fprintf(stderr, "(null)\n"); return; }

    switch (e->type) {
    case EXPR_NUMBER:
        if (e->as.number == (double)(int)e->as.number)
            fprintf(stderr, "NUMBER(%d)\n", (int)e->as.number);
        else
            fprintf(stderr, "NUMBER(%g)\n", e->as.number);
        break;
    case EXPR_STRING:
        fprintf(stderr, "STRING(\"%.*s\")\n", (int)e->as.string.length, e->as.string.start);
        break;
    case EXPR_IDENTIFIER:
        fprintf(stderr, "IDENT(%.*s)\n", (int)e->as.identifier.length, e->as.identifier.start);
        break;
    case EXPR_BINARY: {
        const char *op;
        switch (e->as.binary.op) {
        case OP_PLUS:  op = "+"; break;
        case OP_MINUS: op = "-"; break;
        case OP_STAR:  op = "*"; break;
        case OP_SLASH: op = "/"; break;
        default:       op = "?"; break;
        }
        fprintf(stderr, "BINARY(%s)\n", op);
        debug_print_expr(e->as.binary.left, indent + 1);
        debug_print_expr(e->as.binary.right, indent + 1);
        return;
    }
    case EXPR_UNARY: {
        const char *op = e->as.unary.op == UNOP_BANG ? "!" : "-";
        fprintf(stderr, "UNARY(%s)\n", op);
        debug_print_expr(e->as.unary.operand, indent + 1);
        return;
    }
    case EXPR_CALL:
        fprintf(stderr, "CALL(%.*s, %zu args)\n",
                (int)e->as.call.name_length, e->as.call.name_start,
                e->as.call.arg_count);
        for (size_t i = 0; i < e->as.call.arg_count; ++i)
            debug_print_expr(e->as.call.args[i], indent + 1);
        return;
    case EXPR_COMPARISON: {
        const char *op;
        switch (e->as.comparison.op) {
        case CMP_GT: op = ">"; break;
        case CMP_LT: op = "<"; break;
        case CMP_GE: op = ">="; break;
        case CMP_LE: op = "<="; break;
        case CMP_EQ: op = "=="; break;
        case CMP_NE: op = "!="; break;
        default:     op = "?"; break;
        }
        fprintf(stderr, "COMPARISON(%s)\n", op);
        debug_print_expr(e->as.comparison.left, indent + 1);
        debug_print_expr(e->as.comparison.right, indent + 1);
        return;
    }
    case EXPR_ARRAY:
        fprintf(stderr, "ARRAY(%zu elements)\n", e->as.array.count);
        for (size_t i = 0; i < e->as.array.count; ++i)
            debug_print_expr(e->as.array.elements[i], indent + 1);
        return;
    case EXPR_INDEX:
        fprintf(stderr, "INDEX\n");
        debug_print_expr(e->as.index.object, indent + 1);
        debug_print_expr(e->as.index.index, indent + 1);
        return;
    case EXPR_DICT:
        fprintf(stderr, "DICT(%zu pairs)\n", e->as.dict.count);
        for (size_t i = 0; i < e->as.dict.count; ++i) {
            for (int j = 0; j < indent + 1; ++j) fprintf(stderr, "  ");
            fprintf(stderr, "KEY:\n");
            debug_print_expr(e->as.dict.keys[i], indent + 2);
            debug_print_expr(e->as.dict.values[i], indent + 2);
        }
        return;
    case EXPR_METHOD:
        fprintf(stderr, "METHOD(%.*s, %zu args)\n",
                (int)e->as.method.method_name_length, e->as.method.method_name_start,
                e->as.method.arg_count);
        debug_print_expr(e->as.method.object, indent + 1);
        for (size_t i = 0; i < e->as.method.arg_count; ++i)
            debug_print_expr(e->as.method.args[i], indent + 1);
        return;
    case EXPR_FIELD:
        fprintf(stderr, "FIELD(%.*s)\n",
                (int)e->as.field.field_name_length, e->as.field.field_name_start);
        debug_print_expr(e->as.field.object, indent + 1);
        return;
    }
}

static void debug_print_stmt(const stmt_t *s, int indent)
{
    for (int i = 0; i < indent; ++i) fprintf(stderr, "  ");
    if (!s) { fprintf(stderr, "(null)\n"); return; }

    switch (s->type) {
    case STMT_WINDOW:
        fprintf(stderr, "WINDOW\n");
        debug_print_expr(s->as.window.title, indent + 1);
        debug_print_expr(s->as.window.width, indent + 1);
        debug_print_expr(s->as.window.height, indent + 1);
        return;
    case STMT_EVENT:
        fprintf(stderr, "EVENT\n");
        debug_print_expr(s->as.event.name, indent + 1);
        fprintf(stderr, "  BODY:\n");
        for (size_t i = 0; i < s->as.event.body_count; ++i)
            debug_print_stmt(s->as.event.body[i], indent + 2);
        return;
    case STMT_CALL:
        fprintf(stderr, "CALL(%.*s, %zu args)\n",
                (int)s->as.call.name_length, s->as.call.name_start,
                s->as.call.arg_count);
        for (size_t i = 0; i < s->as.call.arg_count; ++i)
            debug_print_expr(s->as.call.args[i], indent + 1);
        return;
    case STMT_ASSIGN:
        fprintf(stderr, "ASSIGN(%.*s)\n",
                (int)s->as.assign.name_length, s->as.assign.name_start);
        debug_print_expr(s->as.assign.value, indent + 1);
        return;
    case STMT_IF:
        fprintf(stderr, "IF\n");
        if (s->as.if_stmt.condition) debug_print_expr(s->as.if_stmt.condition, indent + 1);
        else fprintf(stderr, "  (condition: none)\n");
        fprintf(stderr, "  THEN:\n");
        for (size_t i = 0; i < s->as.if_stmt.then_count; ++i)
            debug_print_stmt(s->as.if_stmt.then_body[i], indent + 2);
        if (s->as.if_stmt.else_branch) {
            fprintf(stderr, "  ELSE:\n");
            const stmt_t *ec = s->as.if_stmt.else_branch;
            while (ec) {
                if (ec->type == STMT_IF && ec->as.if_stmt.condition) {
                    fprintf(stderr, "    ELSE IF:\n");
                    debug_print_expr(ec->as.if_stmt.condition, indent + 2);
                    for (size_t i = 0; i < ec->as.if_stmt.then_count; ++i)
                        debug_print_stmt(ec->as.if_stmt.then_body[i], indent + 3);
                    ec = ec->as.if_stmt.else_branch;
                } else {
                    for (size_t i = 0; i < ec->as.if_stmt.then_count; ++i)
                        debug_print_stmt(ec->as.if_stmt.then_body[i], indent + 2);
                    ec = NULL;
                }
            }
        }
        return;
    case STMT_WHILE:
        fprintf(stderr, "WHILE\n");
        if (s->as.while_stmt.condition)
            debug_print_expr(s->as.while_stmt.condition, indent + 1);
        fprintf(stderr, "  BODY:\n");
        for (size_t i = 0; i < s->as.while_stmt.body_count; ++i)
            debug_print_stmt(s->as.while_stmt.body[i], indent + 2);
        return;
    case STMT_FOR:
        fprintf(stderr, "FOR\n");
        fprintf(stderr, "  INIT:\n");
        if (s->as.for_stmt.init) debug_print_stmt(s->as.for_stmt.init, indent + 2);
        else fprintf(stderr, "    (none)\n");
        fprintf(stderr, "  COND:\n");
        if (s->as.for_stmt.condition) debug_print_expr(s->as.for_stmt.condition, indent + 2);
        else fprintf(stderr, "    (none)\n");
        fprintf(stderr, "  INC:\n");
        if (s->as.for_stmt.inc) debug_print_stmt(s->as.for_stmt.inc, indent + 2);
        else fprintf(stderr, "    (none)\n");
        fprintf(stderr, "  BODY:\n");
        for (size_t i = 0; i < s->as.for_stmt.body_count; ++i)
            debug_print_stmt(s->as.for_stmt.body[i], indent + 2);
        return;
    case STMT_PRINT: {
        fprintf(stderr, "PRINT(%zu values)\n", s->as.print.value_count);
        for (size_t i = 0; i < s->as.print.value_count; ++i)
            debug_print_expr(s->as.print.values[i], indent + 1);
        return;
    }
    case STMT_RETURN:
        fprintf(stderr, "RETURN\n");
        if (s->as.ret.value)
            debug_print_expr(s->as.ret.value, indent + 1);
        return;
    case STMT_FN:
        fprintf(stderr, "FN(%.*s, %zu params)\n",
                (int)s->as.fn.name_length, s->as.fn.name_start,
                s->as.fn.param_count);
        for (size_t i = 0; i < s->as.fn.param_count; ++i)
            fprintf(stderr, "%*sPARAM(%.*s)\n", (indent + 1) * 2, "",
                    (int)s->as.fn.params[i].name_length,
                    s->as.fn.params[i].name_start);
        fprintf(stderr, "  BODY:\n");
        for (size_t i = 0; i < s->as.fn.body_count; ++i)
            debug_print_stmt(s->as.fn.body[i], indent + 2);
        return;
    case STMT_EXPR:
        fprintf(stderr, "EXPR\n");
        debug_print_expr(s->as.expr_stmt.expr, indent + 1);
        return;
    case STMT_BREAK:
        fprintf(stderr, "BREAK\n");
        return;
    case STMT_CONTINUE:
        fprintf(stderr, "CONTINUE\n");
        return;
    case STMT_STRUCT:
        fprintf(stderr, "STRUCT(%.*s, %zu fields)\n",
                (int)s->as.struct_def.name_length, s->as.struct_def.name_start,
                s->as.struct_def.field_count);
        for (size_t i = 0; i < s->as.struct_def.field_count; ++i)
            fprintf(stderr, "%*sFIELD(%.*s)\n", (indent + 1) * 2, "",
                    (int)s->as.struct_def.fields[i].name_length,
                    s->as.struct_def.fields[i].name_start);
        return;
    case STMT_SPRITE:
        fprintf(stderr, "SPRITE(%.*s)\n",
                (int)s->as.sprite.name_length, s->as.sprite.name_start);
        debug_print_expr(s->as.sprite.path, indent + 1);
        return;
    }
}

void ast_debug_print(const program_t *prog)
{
    if (!prog) { fprintf(stderr, "Program: (null)\n"); return; }
    fprintf(stderr, "Program (%zu statements):\n", prog->count);
    for (size_t i = 0; i < prog->count; ++i)
        debug_print_stmt(prog->stmts[i], 1);
    fprintf(stderr, "\n");
}

void ast_debug_print_stmt(const stmt_t *s)
{
    debug_print_stmt(s, 1);
}

/* ------------------------------------------------------------------ *
 * Public API                                                         *
 * ------------------------------------------------------------------ */

void parser_init(parser_t *parser, lexer_t *lexer, const char *base_dir)
{
    parser->lexer         = lexer;
    parser->had_error     = 0;
    parser->panicking     = 0;
    parser->import_stack  = NULL;
    parser->import_count  = 0;
    parser->import_cap    = 0;
    parser->loop_depth    = 0;
    if (base_dir)
        strncpy(parser->base_dir, base_dir, sizeof(parser->base_dir) - 1);
    else
        parser->base_dir[0] = '\0';
    parser->base_dir[sizeof(parser->base_dir) - 1] = '\0';
    advance(parser);
}

void parser_free_imports(parser_t *parser)
{
    for (size_t i = 0; i < parser->import_count; ++i)
        free(parser->import_stack[i]);
    free(parser->import_stack);
    parser->import_stack = NULL;
    parser->import_count = 0;
    parser->import_cap   = 0;
}

program_t *parse_program(parser_t *parser)
{
    return parse_program_internal(parser);
}

/* ------------------------------------------------------------------ *
 * import_stmt -> 'import' STRING                                      *
 * ------------------------------------------------------------------ */
static void parse_import(parser_t *p, program_t *prog)
{
    advance(p); /* consume 'import' */

    token_t path_tok = consume(p, TOKEN_STRING,
                               "expected file path string after 'import'");
    if (p->panicking) return;   /* consume() already skipped to a safe point */

    /* Extract filename (may contain extension, e.g. "utils/io.vibes") */
    char filename[1024];
    size_t flen = path_tok.length;
    if (flen >= sizeof(filename)) flen = sizeof(filename) - 1;
    memcpy(filename, path_tok.start, flen);
    filename[flen] = '\0';

    /* Resolve: user package dir (~/.mylang/packages) first, then relative
       to the current source file's directory. */
    char resolved[1024];
    if (!resolve_package_import(filename, resolved, sizeof(resolved)))
        resolve_import_path(p->base_dir, filename, resolved, sizeof(resolved));

    /* Check for circular import */
    if (in_import_stack(p, resolved)) {
        error_fmt(p, &path_tok, "circular import detected — '%s' is already being parsed",
                  resolved);
        return;
    }

    /* Read the imported file */
    size_t file_len = 0;
    char *source = read_entire_file(resolved, &file_len);
    if (!source) {
        error_fmt(p, &path_tok, "cannot open import \"%s\"", resolved);
        return;
    }

    /* Store source buffer in the program (stays alive for token refs) */
    store_source(prog, source);

    /* Get directory of the imported file (for nested imports) */
    char import_dir[1024];
    get_dir(resolved, import_dir, sizeof(import_dir));

    /* Push onto import stack */
    push_import(p, resolved);

    /* Create new lexer + parser for the imported file */
    lexer_t imp_lexer;
    lexer_init(&imp_lexer, source);

    parser_t imp_parser;
    parser_init(&imp_parser, &imp_lexer, import_dir);

    /* Share the parent's import stack (must be AFTER parser_init which resets it) */
    imp_parser.import_stack = p->import_stack;
    imp_parser.import_count = p->import_count;
    imp_parser.import_cap   = p->import_cap;

    /* Recursively parse the imported file */
    program_t *imported = parse_program_internal(&imp_parser);

    /* Sync back import stack (child may have added more) */
    p->import_stack = imp_parser.import_stack;
    p->import_count = imp_parser.import_count;
    p->import_cap   = imp_parser.import_cap;

    /* Propagate errors */
    if (imp_parser.had_error)
        p->had_error = 1;

    if (imported) {
        /* Merge imported statements into outer program */
        for (size_t i = 0; i < imported->count; ++i)
            append_stmt(prog, imported->stmts[i]);

        /* Transfer source ownership */
        for (size_t i = 0; i < imported->source_count; ++i)
            store_source(prog, imported->sources[i]);

        /* Prevent double-free */
        imported->count        = 0;
        imported->source_count = 0;
        ast_destroy_program(imported);
    }

    /* Pop import stack */
    pop_import(p);

    skip_newlines(p);
}

/* ------------------------------------------------------------------ *
 * program -> declaration* EOF                                        *
 * ------------------------------------------------------------------ */

static program_t *parse_program_internal(parser_t *p)
{
    program_t *prog = (program_t *)calloc(1, sizeof(program_t));
    if (!prog) { fprintf(stderr, "OOM\n"); exit(EXIT_FAILURE); }

    while (!check(p, TOKEN_EOF)) {
        skip_newlines(p);
        p->panicking = 0;

        /* Handle import statements */
        if (check(p, TOKEN_KEYWORD) && token_equals(p->current, "import")) {
            parse_import(p, prog);
            continue;
        }

        stmt_t *s = parse_declaration(p);
        if (s)
            append_stmt(prog, s);

        /* Only skip a sync token (never a real statement) so we keep
           reporting every error instead of stopping at the first. */
        if (p->had_error && (check(p, TOKEN_NEWLINE) || check(p, TOKEN_SEMICOLON) ||
                             check(p, TOKEN_BRACE_OPEN) ||
                             check(p, TOKEN_BRACE_CLOSE)))
            advance(p);
    }

    return prog;
}

/* ------------------------------------------------------------------ *
 * declaration -> fn_stmt | stmt                                      *
 * ------------------------------------------------------------------ */

static stmt_t *parse_declaration(parser_t *p)
{
    skip_newlines(p);

    if (check(p, TOKEN_KEYWORD) && token_equals(p->current, "fn"))
        return parse_fn_stmt(p);

    if (check(p, TOKEN_KEYWORD) && token_equals(p->current, "struct"))
        return parse_struct_stmt(p);

    return parse_statement(p);
}

/* ------------------------------------------------------------------ *
 * stmt -> if | while | for | print | return | assign | expr          *
 * ------------------------------------------------------------------ */

static stmt_t *parse_statement(parser_t *p)
{
    skip_newlines(p);

    /* INDENT/DEDENT markers can sit at statement boundaries when legacy
       '{' blocks are still in use — the block body machinery owns them,
       so skip any stray ones here before dispatching. */
    while (check(p, TOKEN_INDENT) || check(p, TOKEN_DEDENT))
        advance(p);

    /* Window creation (game syntax) */
    if (check(p, TOKEN_KEYWORD) && token_equals(p->current, "window"))
        return parse_window_stmt(p);

    /* Event handler (game syntax) */
    if (check(p, TOKEN_KEYWORD) && token_equals(p->current, "on"))
        return parse_event_stmt(p);

    /* If-statement */
    if (check(p, TOKEN_KEYWORD) && token_equals(p->current, "if"))
        return parse_if_stmt(p);

    /* While loop */
    if (check(p, TOKEN_KEYWORD) && token_equals(p->current, "while"))
        return parse_while_stmt(p);

    /* For loop */
    if (check(p, TOKEN_KEYWORD) && token_equals(p->current, "for"))
        return parse_for_stmt(p);

    /* Print statement */
    if (check(p, TOKEN_KEYWORD) && token_equals(p->current, "print"))
        return parse_print_stmt(p);

    /* Return statement */
    if (check(p, TOKEN_KEYWORD) && token_equals(p->current, "return"))
        return parse_return_stmt(p);

    /* Break statement */
    if (check(p, TOKEN_KEYWORD) && token_equals(p->current, "break"))
        return parse_break_stmt(p);

    /* Continue statement */
    if (check(p, TOKEN_KEYWORD) && token_equals(p->current, "continue"))
        return parse_continue_stmt(p);

    /* Sprite declaration (game syntax) */
    if (check(p, TOKEN_KEYWORD) && token_equals(p->current, "sprite"))
        return parse_sprite_stmt(p);

    /* Assignment (identifier = expr) or expression statement */
    if (check(p, TOKEN_IDENTIFIER) && peek_raw(p) == '=') {
        token_t name = p->current;
        advance(p);
        return parse_assign_or_expr_stmt(p, name);
    }

    /* Legacy call: keyword/identifier followed by space-separated args.
       Only used if next char is NOT '(' (which would be a call expr). */
    if (check(p, TOKEN_KEYWORD) || check(p, TOKEN_IDENTIFIER)) {
        /* Check if followed by '(' -> expression statement */
        if (peek_raw(p) == '(')
            return parse_as_expr_stmt(p);
        return parse_legacy_call_stmt(p);
    }

    /* Try parsing as expression statement (e.g. add(x, y)) */
    return parse_as_expr_stmt(p);
}

/* ------------------------------------------------------------------ *
 * fn_stmt -> 'fn' IDENT '(' params? ')' ':' NEWLINE                 *
 *            INDENT stmt* DEDENT                                     *
 * ------------------------------------------------------------------ */

static stmt_t *parse_fn_stmt(parser_t *p)
{
    advance(p); /* consume 'fn' */

    token_t name_tok = consume(p, TOKEN_IDENTIFIER, "expected function name after 'fn'");
    consume(p, TOKEN_LPAREN, "expected '(' after function name");

    stmt_t *stmt = alloc_stmt();
    stmt->type   = STMT_FN;
    stmt->line   = p->previous.line;
    stmt->column = p->previous.column;

    stmt->as.fn.name_start  = name_tok.start;
    stmt->as.fn.name_length = name_tok.length;
    stmt->as.fn.params      = NULL;
    stmt->as.fn.param_count = 0;
    stmt->as.fn.body        = NULL;
    stmt->as.fn.body_count  = 0;

    /* Parse parameters */
    size_t param_cap = 0;
    if (!check(p, TOKEN_RPAREN)) {
        do {
            token_t param = consume(p, TOKEN_IDENTIFIER, "expected parameter name");
            GROW_ARRAY(stmt->as.fn.params, stmt->as.fn.param_count, param_cap, sizeof(param_t));
            stmt->as.fn.params[stmt->as.fn.param_count].name_start  = param.start;
            stmt->as.fn.params[stmt->as.fn.param_count].name_length = param.length;
            stmt->as.fn.param_count++;
        } while (match(p, TOKEN_COMMA));
    }
    consume(p, TOKEN_RPAREN, "expected ')' after parameters");

    expect_block_header(p, "function parameters");

    /* Body: INDENT, statements until DEDENT, consume DEDENT */
    size_t body_cap = 0;
    expect_block_body(p);
    while (!check(p, TOKEN_DEDENT) && !check(p, TOKEN_BRACE_OPEN) &&
           !check(p, TOKEN_BRACE_CLOSE) && !check(p, TOKEN_EOF)) {
        p->panicking = 0;
        stmt_t *body_stmt = parse_statement(p);
        if (body_stmt) {
            GROW_ARRAY(stmt->as.fn.body, stmt->as.fn.body_count, body_cap, sizeof(stmt_t *));
            stmt->as.fn.body[stmt->as.fn.body_count++] = body_stmt;
        }
        if (p->had_error && (check(p, TOKEN_NEWLINE) || check(p, TOKEN_SEMICOLON) ||
                             check(p, TOKEN_BRACE_OPEN) || check(p, TOKEN_BRACE_CLOSE)))
            advance(p);
    }
    close_block_body(p);
    skip_newlines(p);

    return stmt;
}

/* ------------------------------------------------------------------ *
 * if_stmt -> 'if' expr ':' NEWLINE INDENT stmt* DEDENT             *
 *            ('else' (if_stmt | ':' NEWLINE INDENT stmt* DEDENT))? *
 * ------------------------------------------------------------------ */

static stmt_t *parse_if_stmt(parser_t *p)
{
    advance(p); /* consume 'if' */

    stmt_t *stmt = alloc_stmt();
    stmt->type   = STMT_IF;
    stmt->line   = p->previous.line;
    stmt->column = p->previous.column;

    /* Parse condition expression */
    stmt->as.if_stmt.condition = parse_expr(p);

    expect_block_header(p, "if condition");

    /* Then-body: INDENT, statements until DEDENT, consume DEDENT */
    size_t body_cap = 0;
    expect_block_body(p);
    while (!check(p, TOKEN_DEDENT) && !check(p, TOKEN_BRACE_OPEN) &&
           !check(p, TOKEN_BRACE_CLOSE) && !check(p, TOKEN_EOF)) {
        p->panicking = 0;
        stmt_t *body_stmt = parse_statement(p);
        if (body_stmt) {
            GROW_ARRAY(stmt->as.if_stmt.then_body, stmt->as.if_stmt.then_count, body_cap, sizeof(stmt_t *));
            stmt->as.if_stmt.then_body[stmt->as.if_stmt.then_count++] = body_stmt;
        }
        if (p->had_error && (check(p, TOKEN_NEWLINE) || check(p, TOKEN_SEMICOLON) ||
                             check(p, TOKEN_BRACE_OPEN) || check(p, TOKEN_BRACE_CLOSE)))
            advance(p);
    }
    close_block_body(p);

    stmt->as.if_stmt.else_branch = NULL;

    /* Optional else clause */
    skip_newlines(p);
    if (check(p, TOKEN_KEYWORD) && token_equals(p->current, "else")) {
        advance(p); /* consume 'else' */
        /* Allow 'else if' or just 'else:' / legacy 'else { }' */
        if (check(p, TOKEN_KEYWORD) && token_equals(p->current, "if")) {
            stmt->as.if_stmt.else_branch = parse_if_stmt(p);
        } else {
            expect_block_header(p, "else");
            stmt_t *else_block = alloc_stmt();
            else_block->type   = STMT_IF; /* reuse STMT_IF as a block container */
            else_block->line   = p->previous.line;
            else_block->column = p->previous.column;
            else_block->as.if_stmt.condition    = NULL;
            else_block->as.if_stmt.then_body    = NULL;
            else_block->as.if_stmt.then_count   = 0;
            else_block->as.if_stmt.else_branch  = NULL;

            size_t else_cap = 0;
            expect_block_body(p);
            while (!check(p, TOKEN_DEDENT) && !check(p, TOKEN_BRACE_OPEN) &&
                   !check(p, TOKEN_BRACE_CLOSE) && !check(p, TOKEN_EOF)) {
                p->panicking = 0;
                stmt_t *body_stmt = parse_statement(p);
                if (body_stmt) {
                    GROW_ARRAY(else_block->as.if_stmt.then_body,
                               else_block->as.if_stmt.then_count,
                               else_cap, sizeof(stmt_t *));
                    else_block->as.if_stmt.then_body[else_block->as.if_stmt.then_count++] = body_stmt;
                }
                if (p->had_error && (check(p, TOKEN_NEWLINE) || check(p, TOKEN_SEMICOLON) ||
                                     check(p, TOKEN_BRACE_OPEN) || check(p, TOKEN_BRACE_CLOSE)))
                    advance(p);
            }
            close_block_body(p);
            stmt->as.if_stmt.else_branch = else_block;
        }
    }

    skip_newlines(p);
    return stmt;
}

/* ------------------------------------------------------------------ *
 * break_stmt -> 'break'                                               *
 * ------------------------------------------------------------------ */
static stmt_t *parse_break_stmt(parser_t *p)
{
    advance(p); /* consume 'break' */

    if (p->loop_depth == 0) {
        error_at(p, &p->previous, "'break' outside of loop");
    }

    stmt_t *stmt = alloc_stmt();
    stmt->type   = STMT_BREAK;
    stmt->line   = p->previous.line;
    stmt->column = p->previous.column;
    skip_newlines(p);
    return stmt;
}

/* ------------------------------------------------------------------ *
 * continue_stmt -> 'continue'                                         *
 * ------------------------------------------------------------------ */
static stmt_t *parse_continue_stmt(parser_t *p)
{
    advance(p); /* consume 'continue' */

    if (p->loop_depth == 0) {
        error_at(p, &p->previous, "'continue' outside of loop");
    }

    stmt_t *stmt = alloc_stmt();
    stmt->type   = STMT_CONTINUE;
    stmt->line   = p->previous.line;
    stmt->column = p->previous.column;
    skip_newlines(p);
    return stmt;
}

/* ------------------------------------------------------------------ *
 * while_stmt -> 'while' expr ':' NEWLINE INDENT stmt* DEDENT       *
 * ------------------------------------------------------------------ */

static stmt_t *parse_while_stmt(parser_t *p)
{
    advance(p); /* consume 'while' */

    stmt_t *stmt = alloc_stmt();
    stmt->type   = STMT_WHILE;
    stmt->line   = p->previous.line;
    stmt->column = p->previous.column;

    stmt->as.while_stmt.condition = parse_expr(p);

    expect_block_header(p, "while condition");

    p->loop_depth++;
    size_t body_cap = 0;
    expect_block_body(p);
    while (!check(p, TOKEN_DEDENT) && !check(p, TOKEN_BRACE_OPEN) &&
           !check(p, TOKEN_BRACE_CLOSE) && !check(p, TOKEN_EOF)) {
        p->panicking = 0;
        stmt_t *body_stmt = parse_statement(p);
        if (body_stmt) {
            GROW_ARRAY(stmt->as.while_stmt.body, stmt->as.while_stmt.body_count, body_cap, sizeof(stmt_t *));
            stmt->as.while_stmt.body[stmt->as.while_stmt.body_count++] = body_stmt;
        }
        if (p->had_error && (check(p, TOKEN_NEWLINE) || check(p, TOKEN_SEMICOLON) ||
                             check(p, TOKEN_BRACE_OPEN) || check(p, TOKEN_BRACE_CLOSE)))
            advance(p);
    }
    p->loop_depth--;
    close_block_body(p);
    skip_newlines(p);

    return stmt;
}

/* ------------------------------------------------------------------ *
 * for_stmt -> 'for' '(' expr? ';' expr? ';' expr? ')' ':' NEWLINE   *
 *            INDENT stmt* DEDENT                                     *
 * ------------------------------------------------------------------ */

static stmt_t *parse_for_stmt(parser_t *p)
{
    advance(p); /* consume 'for' */
    consume(p, TOKEN_LPAREN, "expected '(' after for");

    stmt_t *stmt = alloc_stmt();
    stmt->type   = STMT_FOR;
    stmt->line   = p->previous.line;
    stmt->column = p->previous.column;
    stmt->as.for_stmt.init      = NULL;
    stmt->as.for_stmt.condition = NULL;
    stmt->as.for_stmt.inc       = NULL;
    stmt->as.for_stmt.body      = NULL;
    stmt->as.for_stmt.body_count = 0;

    /* Parse init (assignment) */
    if (!check(p, TOKEN_SEMICOLON)) {
        if (check(p, TOKEN_IDENTIFIER)) {
            token_t name = p->current;
            advance(p);
            if (match(p, TOKEN_EQUALS)) {
                stmt_t *init = alloc_stmt();
                init->type                 = STMT_ASSIGN;
                init->line                 = name.line;
                init->column               = name.column;
                init->as.assign.name_start  = name.start;
                init->as.assign.name_length = name.length;
                init->as.assign.value       = parse_expr(p);
                stmt->as.for_stmt.init      = init;
            }
        }
    }
    consume(p, TOKEN_SEMICOLON, "expected ';' after for init");

    /* Parse condition */
    if (!check(p, TOKEN_SEMICOLON))
        stmt->as.for_stmt.condition = parse_expr(p);
    consume(p, TOKEN_SEMICOLON, "expected ';' after for condition");

    /* Parse increment */
    if (!check(p, TOKEN_RPAREN)) {
        token_t name = p->current; /* must be identifier */
        if (check(p, TOKEN_IDENTIFIER)) {
            advance(p);
            if (match(p, TOKEN_EQUALS)) {
                stmt_t *inc = alloc_stmt();
                inc->type                 = STMT_ASSIGN;
                inc->line                 = name.line;
                inc->column               = name.column;
                inc->as.assign.name_start  = name.start;
                inc->as.assign.name_length = name.length;
                inc->as.assign.value       = parse_expr(p);
                stmt->as.for_stmt.inc      = inc;
            }
        }
    }
    consume(p, TOKEN_RPAREN, "expected ')' after for clauses");

    expect_block_header(p, "for clauses");

    p->loop_depth++;
    size_t body_cap = 0;
    expect_block_body(p);
    while (!check(p, TOKEN_DEDENT) && !check(p, TOKEN_BRACE_OPEN) &&
           !check(p, TOKEN_BRACE_CLOSE) && !check(p, TOKEN_EOF)) {
        p->panicking = 0;
        stmt_t *body_stmt = parse_statement(p);
        if (body_stmt) {
            GROW_ARRAY(stmt->as.for_stmt.body, stmt->as.for_stmt.body_count, body_cap, sizeof(stmt_t *));
            stmt->as.for_stmt.body[stmt->as.for_stmt.body_count++] = body_stmt;
        }
        if (p->had_error && (check(p, TOKEN_NEWLINE) || check(p, TOKEN_SEMICOLON) ||
                             check(p, TOKEN_BRACE_OPEN) || check(p, TOKEN_BRACE_CLOSE)))
            advance(p);
    }
    p->loop_depth--;
    close_block_body(p);
    skip_newlines(p);

    return stmt;
}

/* ------------------------------------------------------------------ *
 * print_stmt -> 'print' expr (',' expr)*                            *
 * ------------------------------------------------------------------ */

static stmt_t *parse_print_stmt(parser_t *p)
{
    advance(p); /* consume 'print' */

    stmt_t *stmt = alloc_stmt();
    stmt->type   = STMT_PRINT;
    stmt->line   = p->previous.line;
    stmt->column = p->previous.column;
    stmt->as.print.values      = NULL;
    stmt->as.print.value_count = 0;

    /* Parse at least one expression, then optional comma-separated exprs */
    size_t cap = 0;
    do {
        expr_t *e = parse_expr(p);
        if (e) {
            GROW_ARRAY(stmt->as.print.values, stmt->as.print.value_count, cap, sizeof(expr_t *));
            stmt->as.print.values[stmt->as.print.value_count++] = e;
        }
    } while (match(p, TOKEN_COMMA));

    skip_newlines(p);
    return stmt;
}

/* ------------------------------------------------------------------ *
 * return_stmt -> 'return' expr?                                      *
 * ------------------------------------------------------------------ */

static stmt_t *parse_return_stmt(parser_t *p)
{
    advance(p); /* consume 'return' */

    stmt_t *stmt = alloc_stmt();
    stmt->type   = STMT_RETURN;
    stmt->line   = p->previous.line;
    stmt->column = p->previous.column;
    stmt->as.ret.value = NULL;

    /* If there's an expression before newline/dedent/brace/EOF, parse it */
    if (!check(p, TOKEN_NEWLINE) && !check(p, TOKEN_DEDENT) &&
        !check(p, TOKEN_BRACE_CLOSE) && !check(p, TOKEN_EOF) &&
        !check(p, TOKEN_SEMICOLON))
    {
        stmt->as.ret.value = parse_expr(p);
        if (!stmt->as.ret.value)
            synchronize(p);
    }

    skip_newlines(p);
    return stmt;
}

/* ------------------------------------------------------------------ *
 * assign_or_expr_stmt -> IDENT '=' expr | expr                       *
 * Called with the identifier already consumed. The '=' has been      *
 * checked via peek_raw.                                              *
 * ------------------------------------------------------------------ */

static stmt_t *parse_assign_or_expr_stmt(parser_t *p, token_t name)
{
    /* Consume '=' */
    if (!match(p, TOKEN_EQUALS)) {
        /* Not an assignment — backtrack: this is an expression stmt.
           We already consumed the identifier, so create an expr from it. */
        expr_t *e = alloc_expr();
        e->type                 = EXPR_IDENTIFIER;
        e->as.identifier.start  = name.start;
        e->as.identifier.length = name.length;
        e->line                 = name.line;
        e->column               = name.column;

        stmt_t *stmt = alloc_stmt();
        stmt->type         = STMT_EXPR;
        stmt->line         = name.line;
        stmt->column       = name.column;
        stmt->as.expr_stmt.expr = e;
        skip_newlines(p);
        return stmt;
    }

    stmt_t *stmt = alloc_stmt();
    stmt->type     = STMT_ASSIGN;
    stmt->line     = name.line;
    stmt->column   = name.column;
    stmt->as.assign.name_start  = name.start;
    stmt->as.assign.name_length = name.length;
    stmt->as.assign.value       = parse_expr(p);

    /* If the value failed to parse, skip the rest of the broken
       statement so we don't mis-parse it as a new statement. */
    if (!stmt->as.assign.value)
        synchronize(p);

    skip_newlines(p);
    return stmt;
}

/* ------------------------------------------------------------------ *
 * parse_as_expr_stmt -> parse an expression as a statement           *
 * ------------------------------------------------------------------ */

static stmt_t *parse_as_expr_stmt(parser_t *p)
{
    expr_t *e = parse_expr(p);
    if (!e) {
        /* The underlying parse_primary already reported the real error;
           only report this one if we aren't already recovering. */
        if (!p->panicking)
            error_at(p, &p->current, "expected statement");
        synchronize(p);
        return NULL;
    }
    stmt_t *stmt = alloc_stmt();
    stmt->type         = STMT_EXPR;
    stmt->line         = e->line;
    stmt->column       = e->column;
    stmt->as.expr_stmt.expr = e;
    skip_newlines(p);
    return stmt;
}

/* ------------------------------------------------------------------ *
 * Legacy: window_stmt -> 'window' expr, expr, expr                   *
 * ------------------------------------------------------------------ */

static stmt_t *parse_window_stmt(parser_t *p)
{
    advance(p);

    stmt_t *stmt = alloc_stmt();
    stmt->type   = STMT_WINDOW;
    stmt->line   = p->previous.line;
    stmt->column = p->previous.column;

    stmt->as.window.title  = parse_expr(p);
    consume(p, TOKEN_COMMA, "expected ',' after window title");
    stmt->as.window.width  = parse_expr(p);
    consume(p, TOKEN_COMMA, "expected ',' after window width");
    stmt->as.window.height = parse_expr(p);

    skip_newlines(p);
    return stmt;
}

/* ------------------------------------------------------------------ *
 * sprite_stmt -> 'sprite' IDENT '=' STRING                           *
 * ------------------------------------------------------------------ */

static stmt_t *parse_sprite_stmt(parser_t *p)
{
    advance(p); /* consume 'sprite' */

    token_t name_tok = consume(p, TOKEN_IDENTIFIER,
        "expected sprite name after 'sprite'");
    consume(p, TOKEN_EQUALS, "expected '=' after sprite name");
    expr_t *path = parse_expr(p);

    stmt_t *stmt = alloc_stmt();
    stmt->type   = STMT_SPRITE;
    stmt->line   = name_tok.line;
    stmt->column = name_tok.column;
    stmt->as.sprite.name_start  = name_tok.start;
    stmt->as.sprite.name_length = name_tok.length;
    stmt->as.sprite.path        = path;

    /* If the path failed to parse, skip the rest of the broken line. */
    if (!path)
        synchronize(p);

    skip_newlines(p);
    return stmt;
}

/* ------------------------------------------------------------------ *
 * event_stmt -> 'on' IDENT ':' NEWLINE INDENT stmt* DEDENT          *
 * ------------------------------------------------------------------ */

static stmt_t *parse_event_stmt(parser_t *p)
{
    advance(p);

    stmt_t *stmt = alloc_stmt();
    stmt->type   = STMT_EVENT;
    stmt->line   = p->previous.line;
    stmt->column = p->previous.column;

    token_t name_tok = consume(p, TOKEN_IDENTIFIER, "expected event name after 'on'");
    if (!p->panicking) {
        stmt->as.event.name = alloc_expr();
        stmt->as.event.name->type                  = EXPR_IDENTIFIER;
        stmt->as.event.name->as.identifier.start   = name_tok.start;
        stmt->as.event.name->as.identifier.length  = name_tok.length;
        stmt->as.event.name->line                  = name_tok.line;
        stmt->as.event.name->column                = name_tok.column;
    }

    expect_block_header(p, "event name");

    size_t body_cap = 0;
    expect_block_body(p);
    while (!check(p, TOKEN_DEDENT) && !check(p, TOKEN_BRACE_OPEN) &&
           !check(p, TOKEN_BRACE_CLOSE) && !check(p, TOKEN_EOF)) {
        p->panicking = 0;
        stmt_t *body_stmt = parse_statement(p);
        if (body_stmt) {
            GROW_ARRAY(stmt->as.event.body, stmt->as.event.body_count, body_cap, sizeof(stmt_t *));
            stmt->as.event.body[stmt->as.event.body_count++] = body_stmt;
        }
        if (p->had_error && (check(p, TOKEN_NEWLINE) || check(p, TOKEN_SEMICOLON) ||
                             check(p, TOKEN_BRACE_OPEN) || check(p, TOKEN_BRACE_CLOSE)))
            advance(p);
    }
    close_block_body(p);
    skip_newlines(p);

    return stmt;
}

/* ------------------------------------------------------------------ *
 * struct_stmt -> 'struct' IDENT ':' NEWLINE INDENT IDENT* DEDENT   *
 * ------------------------------------------------------------------ */

static stmt_t *parse_struct_stmt(parser_t *p)
{
    advance(p); /* consume 'struct' */

    token_t name_tok = consume(p, TOKEN_IDENTIFIER,
        "expected struct name after 'struct'");
    if (p->panicking) return NULL;   /* consume() already synchronized */

    expect_block_header(p, "struct name");

    stmt_t *stmt = alloc_stmt();
    stmt->type   = STMT_STRUCT;
    stmt->line   = name_tok.line;
    stmt->column = name_tok.column;

    stmt->as.struct_def.name_start  = name_tok.start;
    stmt->as.struct_def.name_length = name_tok.length;
    stmt->as.struct_def.fields      = NULL;
    stmt->as.struct_def.field_count = 0;

    size_t field_cap = 0;
    expect_block_body(p);
    while (!check(p, TOKEN_DEDENT) && !check(p, TOKEN_BRACE_OPEN) &&
           !check(p, TOKEN_BRACE_CLOSE) && !check(p, TOKEN_EOF)) {
        p->panicking = 0;
        token_t field_tok = consume(p, TOKEN_IDENTIFIER,
            "expected field name in struct");
        if (p->panicking) {
            if (check(p, TOKEN_NEWLINE) || check(p, TOKEN_SEMICOLON) ||
                check(p, TOKEN_BRACE_OPEN) || check(p, TOKEN_BRACE_CLOSE))
                advance(p);
            continue;
        }

        GROW_ARRAY(stmt->as.struct_def.fields, stmt->as.struct_def.field_count,
                   field_cap, sizeof(param_t));
        stmt->as.struct_def.fields[stmt->as.struct_def.field_count].name_start  = field_tok.start;
        stmt->as.struct_def.fields[stmt->as.struct_def.field_count].name_length = field_tok.length;
        stmt->as.struct_def.field_count++;

        skip_newlines(p);
    }
    close_block_body(p);
    skip_newlines(p);
    return stmt;
}

/* ------------------------------------------------------------------ *
 * Legacy: call_stmt -> KEYWORD|IDENTIFIER [expr (',' expr)*]         *
 * Space-separated args (no parens), used for game syntax.           *
 * ------------------------------------------------------------------ */

static stmt_t *parse_legacy_call_stmt(parser_t *p)
{
    stmt_t *stmt = alloc_stmt();
    stmt->type   = STMT_CALL;
    stmt->line   = p->current.line;
    stmt->column = p->current.column;

    stmt->as.call.name_start  = p->current.start;
    stmt->as.call.name_length = p->current.length;
    advance(p);

    size_t arg_cap = 0;
    while (!check(p, TOKEN_NEWLINE) && !check(p, TOKEN_DEDENT) &&
           !check(p, TOKEN_EOF) && !check(p, TOKEN_BRACE_CLOSE))
    {
        expr_t *e = parse_expr(p);
        if (e) {
            GROW_ARRAY(stmt->as.call.args, stmt->as.call.arg_count, arg_cap, sizeof(expr_t *));
            stmt->as.call.args[stmt->as.call.arg_count++] = e;
        }
        if (check(p, TOKEN_COMMA))
            advance(p);
        else
            break;
    }

    skip_newlines(p);
    return stmt;
}

/* ------------------------------------------------------------------ *
 * Expressions — precedence climbing                                  *
 * ------------------------------------------------------------------ *
 *   assignment -> equality ('=' equality)?                           *
 *   equality  -> comparison (('==' | '!=') comparison)*              *
 *   comparison -> addition (('>' | '<' | '>=' | '<=') addition)*    *
 *   addition  -> term (( '+' | '-' ) term)*                          *
 *   term      -> unary (( '*' | '/' ) unary)*                        *
 *   unary     -> ('!' | '-') unary | call                            *
 *   call      -> primary ('(' args? ')')*                            *
 *   primary   -> NUMBER | STRING | IDENTIFIER | '(' expr ')'         *
 * ------------------------------------------------------------------ */

static expr_t *parse_expr(parser_t *p)
{
    return parse_assignment(p);
}

static expr_t *parse_assignment(parser_t *p)
{
    expr_t *e = parse_equality(p);
    if (!e) return NULL;

    if (match(p, TOKEN_EQUALS)) {
        /* We saw '=' but this shouldn't happen in parse_assignment
           because assignment is handled at the statement level.
           If we get here, treat it as a comparison error. */
        error_at(p, &p->previous, "unexpected '=' in expression");
    }

    return e;
}

static expr_t *parse_equality(parser_t *p)
{
    expr_t *e = parse_comparison(p);
    if (!e) return NULL;

    while (check(p, TOKEN_EQ_EQ) || check(p, TOKEN_BANG_EQ)) {
        cmp_op_t op = check(p, TOKEN_EQ_EQ) ? CMP_EQ : CMP_NE;
        advance(p);
        expr_t *right = parse_comparison(p);
        if (!right) return e;

        expr_t *bin = alloc_expr();
        bin->type              = EXPR_COMPARISON;
        bin->as.comparison.op   = op;
        bin->as.comparison.left = e;
        bin->as.comparison.right = right;
        bin->line               = e->line;
        bin->column             = e->column;
        e = bin;
    }

    return e;
}

static expr_t *parse_comparison(parser_t *p)
{
    expr_t *e = parse_addition(p);
    if (!e) return NULL;

    while (check(p, TOKEN_GREATER) || check(p, TOKEN_LESS) ||
           check(p, TOKEN_GREATER_EQ) || check(p, TOKEN_LESS_EQ))
    {
        cmp_op_t op;
        if (check(p, TOKEN_GREATER))    op = CMP_GT;
        else if (check(p, TOKEN_LESS))  op = CMP_LT;
        else if (check(p, TOKEN_GREATER_EQ)) op = CMP_GE;
        else                             op = CMP_LE;
        advance(p);

        expr_t *right = parse_addition(p);
        if (!right) return e;

        expr_t *cmp = alloc_expr();
        cmp->type              = EXPR_COMPARISON;
        cmp->as.comparison.op   = op;
        cmp->as.comparison.left = e;
        cmp->as.comparison.right = right;
        cmp->line               = e->line;
        cmp->column             = e->column;
        e = cmp;
    }

    return e;
}

static expr_t *parse_addition(parser_t *p)
{
    expr_t *e = parse_term(p);
    if (!e) return NULL;

    while (check(p, TOKEN_PLUS) || check(p, TOKEN_MINUS)) {
        binary_op_t op = check(p, TOKEN_PLUS) ? OP_PLUS : OP_MINUS;
        advance(p);

        expr_t *right = parse_term(p);
        if (!right) return e;

        expr_t *bin = alloc_expr();
        bin->type          = EXPR_BINARY;
        bin->as.binary.op   = op;
        bin->as.binary.left = e;
        bin->as.binary.right = right;
        bin->line           = e->line;
        bin->column         = e->column;
        e = bin;
    }

    return e;
}

static expr_t *parse_term(parser_t *p)
{
    expr_t *e = parse_unary(p);
    if (!e) return NULL;

    while (check(p, TOKEN_STAR) || check(p, TOKEN_SLASH)) {
        binary_op_t op = check(p, TOKEN_STAR) ? OP_STAR : OP_SLASH;
        advance(p);

        expr_t *right = parse_unary(p);
        if (!right) return e;

        expr_t *bin = alloc_expr();
        bin->type          = EXPR_BINARY;
        bin->as.binary.op   = op;
        bin->as.binary.left = e;
        bin->as.binary.right = right;
        bin->line           = e->line;
        bin->column         = e->column;
        e = bin;
    }

    return e;
}

static expr_t *parse_unary(parser_t *p)
{
    if (check(p, TOKEN_BANG) || check(p, TOKEN_MINUS)) {
        unary_op_t op = check(p, TOKEN_BANG) ? UNOP_BANG : UNOP_MINUS;
        advance(p);

        expr_t *operand = parse_unary(p);
        if (!operand) return NULL;

        expr_t *un = alloc_expr();
        un->type           = EXPR_UNARY;
        un->as.unary.op     = op;
        un->as.unary.operand = operand;
        un->line            = p->previous.line;
        un->column          = p->previous.column;
        return un;
    }

    return parse_call(p);
}

static expr_t *parse_call(parser_t *p)
{
    expr_t *e = parse_primary(p);
    if (!e) return NULL;   /* error already reported; let caller recover */

    /* Postfix operations: call (...), index [...], method .method(args) */
    while (check(p, TOKEN_LPAREN) || check(p, TOKEN_LBRACKET) || check(p, TOKEN_DOT)) {
        if (check(p, TOKEN_LPAREN)) {
            advance(p); /* consume '(' */

            expr_t *call = alloc_expr();
            call->type = EXPR_CALL;
            call->line = e->line;
            call->column = e->column;

            /* The function name comes from the primary expression */
            if (e->type == EXPR_IDENTIFIER) {
                call->as.call.name_start  = e->as.identifier.start;
                call->as.call.name_length = e->as.identifier.length;
            } else {
                error_at(p, &p->previous, "invalid function name");
                call->as.call.name_start  = NULL;
                call->as.call.name_length = 0;
            }

            call->as.call.args      = NULL;
            call->as.call.arg_count = 0;

            size_t arg_cap = 0;
            if (!check(p, TOKEN_RPAREN)) {
                do {
                    expr_t *arg = parse_expr(p);
                    if (arg) {
                        GROW_ARRAY(call->as.call.args, call->as.call.arg_count, arg_cap, sizeof(expr_t *));
                        call->as.call.args[call->as.call.arg_count++] = arg;
                    }
                } while (match(p, TOKEN_COMMA));
            }
            consume(p, TOKEN_RPAREN, "expected ')' after arguments");

            /* Free the old primary expr (we copied the identifier into the call) */
            free(e);
            e = call;

        } else if (check(p, TOKEN_LBRACKET)) {
            /* Index access: expr '[' index ']' */
            advance(p); /* consume '[' */

            expr_t *idx = alloc_expr();
            idx->type         = EXPR_INDEX;
            idx->as.index.object = e;
            idx->as.index.index  = parse_expr(p);
            idx->line          = e->line;
            idx->column        = e->column;

            consume(p, TOKEN_RBRACKET, "expected ']' after index");
            e = idx;

        } else {
            /* Dot access: expr . name ( args? ) */
            advance(p); /* consume '.' */

            token_t name_tok = consume(p, TOKEN_IDENTIFIER,
                "expected name after '.'");
            if (p->panicking) break;

            /* If followed by '(', it's a method call */
            if (check(p, TOKEN_LPAREN)) {
                advance(p); /* consume '(' */

                expr_t *meth = alloc_expr();
                meth->type           = EXPR_METHOD;
                meth->as.method.object = e;
                meth->as.method.method_name_start  = name_tok.start;
                meth->as.method.method_name_length = name_tok.length;
                meth->as.method.args      = NULL;
                meth->as.method.arg_count = 0;
                meth->line = e->line;
                meth->column = e->column;

                size_t arg_cap = 0;
                if (!check(p, TOKEN_RPAREN)) {
                    do {
                        expr_t *arg = parse_expr(p);
                        if (arg) {
                            GROW_ARRAY(meth->as.method.args, meth->as.method.arg_count,
                                       arg_cap, sizeof(expr_t *));
                            meth->as.method.args[meth->as.method.arg_count++] = arg;
                        }
                    } while (match(p, TOKEN_COMMA));
                }
                consume(p, TOKEN_RPAREN, "expected ')' after method arguments");
                e = meth;
            } else {
                /* No parens: it's a field/property access */
                expr_t *field = alloc_expr();
                field->type            = EXPR_FIELD;
                field->as.field.object = e;
                field->as.field.field_name_start  = name_tok.start;
                field->as.field.field_name_length = name_tok.length;
                field->line = e->line;
                field->column = e->column;
                e = field;
            }
        }
    }

    return e;
}

static expr_t *parse_primary(parser_t *p)
{
    if (match(p, TOKEN_NUMBER)) {
        expr_t *e = alloc_expr();
        e->type      = EXPR_NUMBER;
        e->as.number = parse_double(p->previous.start, p->previous.length);
        e->line      = p->previous.line;
        e->column    = p->previous.column;
        return e;
    }

    if (match(p, TOKEN_STRING)) {
        expr_t *e = alloc_expr();
        e->type               = EXPR_STRING;
        e->as.string.start    = p->previous.start;
        e->as.string.length   = p->previous.length;
        e->line               = p->previous.line;
        e->column             = p->previous.column;
        return e;
    }

    if (match(p, TOKEN_IDENTIFIER)) {
        expr_t *e = alloc_expr();
        e->type                 = EXPR_IDENTIFIER;
        e->as.identifier.start  = p->previous.start;
        e->as.identifier.length = p->previous.length;
        e->line                 = p->previous.line;
        e->column               = p->previous.column;
        return e;
    }

    /* Keywords are also valid as identifiers (backward compat). */
    if (match(p, TOKEN_KEYWORD)) {
        expr_t *e = alloc_expr();
        e->type                 = EXPR_IDENTIFIER;
        e->as.identifier.start  = p->previous.start;
        e->as.identifier.length = p->previous.length;
        e->line                 = p->previous.line;
        e->column               = p->previous.column;
        return e;
    }

    if (match(p, TOKEN_LPAREN)) {
        expr_t *e = parse_expr(p);
        consume(p, TOKEN_RPAREN, "expected ')' after expression");
        return e;
    }

    /* Array literal: [expr, expr, ...] */
    if (match(p, TOKEN_LBRACKET)) {
        expr_t *arr = alloc_expr();
        arr->type          = EXPR_ARRAY;
        arr->as.array.elements = NULL;
        arr->as.array.count    = 0;
        arr->line           = p->previous.line;
        arr->column         = p->previous.column;

        size_t elem_cap = 0;
        if (!check(p, TOKEN_RBRACKET)) {
            do {
                expr_t *elem = parse_expr(p);
                if (elem) {
                    GROW_ARRAY(arr->as.array.elements, arr->as.array.count,
                               elem_cap, sizeof(expr_t *));
                    arr->as.array.elements[arr->as.array.count++] = elem;
                }
            } while (match(p, TOKEN_COMMA));
        }
        consume(p, TOKEN_RBRACKET, "expected ']' after array elements");
        return arr;
    }

    /* Dict literal: {"key": expr, ...} */
    if (match(p, TOKEN_BRACE_OPEN)) {
        expr_t *d = alloc_expr();
        d->type          = EXPR_DICT;
        d->as.dict.keys   = NULL;
        d->as.dict.values = NULL;
        d->as.dict.count  = 0;
        d->line           = p->previous.line;
        d->column         = p->previous.column;

        size_t elem_cap = 0;
        if (!check(p, TOKEN_BRACE_CLOSE)) {
            do {
                expr_t *key = parse_expr(p);
                if (!key) break;
                consume(p, TOKEN_COLON, "expected ':' after dict key");
                expr_t *val = parse_expr(p);
                if (!val) { expr_destroy(key); break; }

                if (d->as.dict.count >= elem_cap) {
                    size_t nc = elem_cap ? elem_cap * 2 : 8;
                    expr_t **tk = (expr_t **)realloc(d->as.dict.keys,
                        nc * sizeof(expr_t *));
                    expr_t **tv = (expr_t **)realloc(d->as.dict.values,
                        nc * sizeof(expr_t *));
                    if (!tk || !tv) { fprintf(stderr, "OOM\n"); exit(EXIT_FAILURE); }
                    d->as.dict.keys = tk; d->as.dict.values = tv;
                    elem_cap = nc;
                }
                d->as.dict.keys[d->as.dict.count]   = key;
                d->as.dict.values[d->as.dict.count] = val;
                d->as.dict.count++;
            } while (match(p, TOKEN_COMMA));
        }
        consume(p, TOKEN_BRACE_CLOSE, "expected '}' after dict entries");
        return d;
    }

    if (!p->panicking) {
        fprintf(stderr, "Error [%d:%d]: expected expression, got \"%.*s\"\n",
                p->current.line, p->current.column,
                (int)p->current.length, p->current.start);
    }
    p->had_error = 1;
    p->panicking = 1;
    return NULL;
}

/* ------------------------------------------------------------------ *
 * AST destruction                                                    *
 * ------------------------------------------------------------------ */

static void expr_destroy(expr_t *e)
{
    if (!e) return;

    switch (e->type) {
    case EXPR_BINARY:
        expr_destroy(e->as.binary.left);
        expr_destroy(e->as.binary.right);
        break;
    case EXPR_UNARY:
        expr_destroy(e->as.unary.operand);
        break;
    case EXPR_CALL:
        for (size_t j = 0; j < e->as.call.arg_count; ++j)
            expr_destroy(e->as.call.args[j]);
        free(e->as.call.args);
        break;
    case EXPR_COMPARISON:
        expr_destroy(e->as.comparison.left);
        expr_destroy(e->as.comparison.right);
        break;
    case EXPR_ARRAY:
        for (size_t j = 0; j < e->as.array.count; ++j)
            expr_destroy(e->as.array.elements[j]);
        free(e->as.array.elements);
        break;
    case EXPR_INDEX:
        expr_destroy(e->as.index.object);
        expr_destroy(e->as.index.index);
        break;
    case EXPR_DICT:
        for (size_t j = 0; j < e->as.dict.count; ++j) {
            expr_destroy(e->as.dict.keys[j]);
            expr_destroy(e->as.dict.values[j]);
        }
        free(e->as.dict.keys);
        free(e->as.dict.values);
        break;
    case EXPR_METHOD:
        expr_destroy(e->as.method.object);
        for (size_t j = 0; j < e->as.method.arg_count; ++j)
            expr_destroy(e->as.method.args[j]);
        free(e->as.method.args);
        break;
    case EXPR_FIELD:
        expr_destroy(e->as.field.object);
        break;
    default:
        break;
    }
    free(e);
}

static void stmt_destroy(stmt_t *s)
{
    if (!s) return;

    switch (s->type) {
    case STMT_WINDOW:
        expr_destroy(s->as.window.title);
        expr_destroy(s->as.window.width);
        expr_destroy(s->as.window.height);
        break;
    case STMT_EVENT:
        expr_destroy(s->as.event.name);
        for (size_t j = 0; j < s->as.event.body_count; ++j)
            stmt_destroy(s->as.event.body[j]);
        free(s->as.event.body);
        break;
    case STMT_CALL:
        for (size_t j = 0; j < s->as.call.arg_count; ++j)
            expr_destroy(s->as.call.args[j]);
        free(s->as.call.args);
        break;
    case STMT_ASSIGN:
        expr_destroy(s->as.assign.value);
        break;
    case STMT_IF:
        expr_destroy(s->as.if_stmt.condition);
        for (size_t j = 0; j < s->as.if_stmt.then_count; ++j)
            stmt_destroy(s->as.if_stmt.then_body[j]);
        free(s->as.if_stmt.then_body);
        stmt_destroy(s->as.if_stmt.else_branch);
        break;
    case STMT_WHILE:
        expr_destroy(s->as.while_stmt.condition);
        for (size_t j = 0; j < s->as.while_stmt.body_count; ++j)
            stmt_destroy(s->as.while_stmt.body[j]);
        free(s->as.while_stmt.body);
        break;
    case STMT_FOR:
        stmt_destroy(s->as.for_stmt.init);
        expr_destroy(s->as.for_stmt.condition);
        stmt_destroy(s->as.for_stmt.inc);
        for (size_t j = 0; j < s->as.for_stmt.body_count; ++j)
            stmt_destroy(s->as.for_stmt.body[j]);
        free(s->as.for_stmt.body);
        break;
    case STMT_PRINT:
        for (size_t j = 0; j < s->as.print.value_count; ++j)
            expr_destroy(s->as.print.values[j]);
        free(s->as.print.values);
        break;
    case STMT_RETURN:
        expr_destroy(s->as.ret.value);
        break;
    case STMT_FN:
        free(s->as.fn.params);
        for (size_t j = 0; j < s->as.fn.body_count; ++j)
            stmt_destroy(s->as.fn.body[j]);
        free(s->as.fn.body);
        break;
    case STMT_EXPR:
        expr_destroy(s->as.expr_stmt.expr);
        break;
    case STMT_BREAK:
    case STMT_CONTINUE:
        /* No allocated fields to free */
        break;
    case STMT_STRUCT:
        free(s->as.struct_def.fields);
        break;
    case STMT_SPRITE:
        expr_destroy(s->as.sprite.path);
        break;
    }
    free(s);
}

void ast_destroy_program(program_t *prog)
{
    if (!prog) return;

    for (size_t i = 0; i < prog->count; ++i)
        stmt_destroy(prog->stmts[i]);

    free(prog->stmts);

    for (size_t i = 0; i < prog->source_count; ++i)
        free(prog->sources[i]);
    free(prog->sources);

    free(prog);
}
