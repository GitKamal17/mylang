#include "pipeline.h"
#include "lexer.h"
#include "parser.h"
#include "codegen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ------------------------------------------------------------------ *
 * Box-drawing helpers                                                *
 * ------------------------------------------------------------------ */

static void print_top_box(const char *title, int width)
{
    fprintf(stderr, "  ");
    for (int i = 0; i < width; ++i) fputc(i == 0 ? '+' : (i == width - 1 ? '+' : '-'), stderr);
    fputc('\n', stderr);
    fprintf(stderr, "  | %-*s|\n", width - 1, title);
}

static void print_bottom_box(int width)
{
    fprintf(stderr, "  ");
    for (int i = 0; i < width; ++i) fputc(i == 0 ? '+' : (i == width - 1 ? '+' : '-'), stderr);
    fputc('\n', stderr);
}

static void print_arrow(void)
{
    fprintf(stderr, "       |\n");
    fprintf(stderr, "       v\n");
}

/* ------------------------------------------------------------------ *
 * Find the first non-blank, non-comment line in the source           *
 * ------------------------------------------------------------------ */
static const char *find_first_source_line(const char *source, int *out_line_num)
{
    const char *p = source;
    int line = 1;

    while (*p) {
        const char *line_start = p;
        /* Find end of line */
        while (*p && *p != '\n') ++p;
        const char *line_end = p;

        /* Skip blank lines */
        int is_blank = 1;
        for (const char *c = line_start; c < line_end; ++c) {
            if (!isspace((unsigned char)*c) && *c != '#') {
                is_blank = 0;
                break;
            }
        }

        if (!is_blank) {
            /* Check it's not just a comment */
            const char *trimmed = line_start;
            while (trimmed < line_end && isspace((unsigned char)*trimmed)) ++trimmed;
            if (trimmed < line_end && *trimmed != '#') {
                *out_line_num = line;
                return line_start;
            }
        }

        if (*p == '\n') {
            ++p;
            ++line;
        }
    }

    *out_line_num = 1;
    return source;
}

/* ------------------------------------------------------------------ *
 * Get the length of a line (up to newline or null)                   *
 * ------------------------------------------------------------------ */
static size_t line_length(const char *line)
{
    const char *p = line;
    while (*p && *p != '\n') ++p;
    return (size_t)(p - line);
}

/* ------------------------------------------------------------------ *
 * Tokenize a single line and produce a string of token names         *
 * ------------------------------------------------------------------ */
static int tokenize_line(const char *line_start, size_t line_len,
                         char *out_tokens, size_t out_size,
                         int *out_count)
{
    /* We need a null-terminated copy of this line to lex */
    char *buf = (char *)malloc(line_len + 1);
    if (!buf) return 0;
    memcpy(buf, line_start, line_len);
    buf[line_len] = '\0';

    lexer_t lexer;
    lexer_init(&lexer, buf);

    char tmp[1024];
    tmp[0] = '\0';
    int first = 1;
    int count = 0;

    for (;;) {
        token_t tok = lexer_next(&lexer);
        if (tok.type == TOKEN_EOF || tok.type == TOKEN_NEWLINE || tok.type == TOKEN_ERROR)
            break;
        count++;

        if (!first) strncat(tmp, " ", sizeof(tmp) - strlen(tmp) - 1);
        first = 0;

        const char *tname = token_type_name(tok.type);
        strncat(tmp, tname, sizeof(tmp) - strlen(tmp) - 1);

        /* Append the token text in parentheses for some token types */
        if (tok.type == TOKEN_IDENTIFIER || tok.type == TOKEN_NUMBER ||
            tok.type == TOKEN_STRING || tok.type == TOKEN_KEYWORD) {
            size_t cur = strlen(tmp);
            snprintf(tmp + cur, sizeof(tmp) - cur, "('%.*s')",
                     tok.length > 12 ? 12 : (int)tok.length, tok.start);
        }
    }

    free(buf);
    strncpy(out_tokens, tmp, out_size - 1);
    out_tokens[out_size - 1] = '\0';
    if (out_count) *out_count = count;
    return count;
}

/* ------------------------------------------------------------------ *
 * Get the AST description for the first statement                    *
 * ------------------------------------------------------------------ */
static void describe_stmt_type(const stmt_t *s, char *out, size_t out_size)
{
    if (!s) { snprintf(out, out_size, "(null)"); return; }

    switch (s->type) {
    case STMT_WINDOW:
        snprintf(out, out_size, "WINDOW(title, w, h) → InitWindow()");
        break;
    case STMT_CALL:
        snprintf(out, out_size, "CALL %.*s(args)",
                 (int)s->as.call.name_length, s->as.call.name_start);
        break;
    case STMT_ASSIGN:
        snprintf(out, out_size, "ASSIGN → %.*s = <expr>",
                 (int)s->as.assign.name_length, s->as.assign.name_start);
        break;
    case STMT_IF:
        snprintf(out, out_size, "IF(condition) { body } [ELSE]");
        break;
    case STMT_WHILE:
        snprintf(out, out_size, "WHILE(condition) { body }");
        break;
    case STMT_FOR:
        snprintf(out, out_size, "FOR(init; cond; inc) { body }");
        break;
    case STMT_PRINT:
        snprintf(out, out_size, "PRINT(args) → printf()");
        break;
    case STMT_RETURN:
        snprintf(out, out_size, "RETURN <expr>");
        break;
    case STMT_FN:
        snprintf(out, out_size, "FN %.*s(params) { body }",
                 (int)s->as.fn.name_length, s->as.fn.name_start);
        break;
    case STMT_EXPR:
        snprintf(out, out_size, "EXPR → statement expression");
        break;
    case STMT_BREAK:
        snprintf(out, out_size, "BREAK");
        break;
    case STMT_CONTINUE:
        snprintf(out, out_size, "CONTINUE");
        break;
    case STMT_STRUCT:
        snprintf(out, out_size, "STRUCT %.*s { fields }",
                 (int)s->as.struct_def.name_length, s->as.struct_def.name_start);
        break;
    case STMT_SPRITE:
        snprintf(out, out_size, "SPRITE %.*s = \"path\" → LoadTexture()",
                 (int)s->as.sprite.name_length, s->as.sprite.name_start);
        break;
    default:
        snprintf(out, out_size, "<statement>");
        break;
    }
}

static void describe_expr(const expr_t *e, char *out, size_t out_size)
{
    if (!e) { snprintf(out, out_size, "(null)"); return; }

    switch (e->type) {
    case EXPR_NUMBER:
        if (e->as.number == (double)(int)e->as.number)
            snprintf(out, out_size, "NUM(%d)", (int)e->as.number);
        else
            snprintf(out, out_size, "NUM(%g)", e->as.number);
        break;
    case EXPR_STRING:
        snprintf(out, out_size, "STR(\"%.*s\")",
                 (int)(e->as.string.length > 15 ? 15 : e->as.string.length),
                 e->as.string.start);
        break;
    case EXPR_IDENTIFIER:
        snprintf(out, out_size, "IDENT('%.*s')",
                 (int)e->as.identifier.length, e->as.identifier.start);
        break;
    case EXPR_BINARY: {
        const char *op = e->as.binary.op == OP_PLUS ? "+" :
                         e->as.binary.op == OP_MINUS ? "-" :
                         e->as.binary.op == OP_STAR ? "*" : "/";
        char left[128], right[128];
        describe_expr(e->as.binary.left, left, sizeof left);
        describe_expr(e->as.binary.right, right, sizeof right);
        snprintf(out, out_size, "BINARY(%s %s %s)", left, op, right);
        break;
    }
    case EXPR_CALL:
        snprintf(out, out_size, "CALL %.*s(%zu args)",
                 (int)e->as.call.name_length, e->as.call.name_start,
                 e->as.call.arg_count);
        break;
    case EXPR_COMPARISON: {
        const char *op = e->as.comparison.op == CMP_GT ? ">" :
                         e->as.comparison.op == CMP_LT ? "<" : "?";
        char left[64], right[64];
        describe_expr(e->as.comparison.left, left, sizeof left);
        describe_expr(e->as.comparison.right, right, sizeof right);
        snprintf(out, out_size, "CMP(%s %s %s)", left, op, right);
        break;
    }
    case EXPR_ARRAY:
        snprintf(out, out_size, "ARRAY(%zu els)", e->as.array.count);
        break;
    case EXPR_INDEX:
        snprintf(out, out_size, "INDEX(obj, idx)");
        break;
    case EXPR_METHOD:
        snprintf(out, out_size, "METHOD(obj, '%.*s')",
                 (int)e->as.method.method_name_length, e->as.method.method_name_start);
        break;
    case EXPR_FIELD:
        snprintf(out, out_size, "FIELD(obj, '%.*s')",
                 (int)e->as.field.field_name_length, e->as.field.field_name_start);
        break;
    default:
        snprintf(out, out_size, "<expression>");
        break;
    }
}

/* ------------------------------------------------------------------ *
 * Get the expression detail within a statement                       *
 * ------------------------------------------------------------------ */
static void get_expr_detail(const stmt_t *s, char *out, size_t out_size)
{
    if (!s) { snprintf(out, out_size, ""); return; }

    switch (s->type) {
    case STMT_ASSIGN:
        if (s->as.assign.value) {
            describe_expr(s->as.assign.value, out, out_size);
        } else {
            snprintf(out, out_size, "");
        }
        break;
    case STMT_PRINT:
        if (s->as.print.value_count > 0 && s->as.print.values[0]) {
            describe_expr(s->as.print.values[0], out, out_size);
        } else {
            snprintf(out, out_size, "");
        }
        break;
    case STMT_IF:
        if (s->as.if_stmt.condition) {
            describe_expr(s->as.if_stmt.condition, out, out_size);
        } else {
            snprintf(out, out_size, "");
        }
        break;
    case STMT_WHILE:
        if (s->as.while_stmt.condition) {
            describe_expr(s->as.while_stmt.condition, out, out_size);
        } else {
            snprintf(out, out_size, "");
        }
        break;
    case STMT_CALL:
        if (s->as.call.arg_count > 0 && s->as.call.args[0]) {
            describe_expr(s->as.call.args[0], out, out_size);
        } else {
            snprintf(out, out_size, "");
        }
        break;
    case STMT_RETURN:
        if (s->as.ret.value) {
            describe_expr(s->as.ret.value, out, out_size);
        } else {
            snprintf(out, out_size, "");
        }
        break;
    case STMT_SPRITE:
        if (s->as.sprite.path) {
            describe_expr(s->as.sprite.path, out, out_size);
        } else {
            snprintf(out, out_size, "");
        }
        break;
    default:
        snprintf(out, out_size, "");
        break;
    }
}

/* ------------------------------------------------------------------ *
 * Get the first few lines of generated C for the first statement     *
 * ------------------------------------------------------------------ */
static void get_first_c_lines(const char *c_code, char *out, size_t out_size)
{
    if (!c_code || !*c_code) {
        snprintf(out, out_size, "(no output)");
        return;
    }

    char tmp[1024];
    tmp[0] = '\0';

    const char *p = c_code;
    int lines = 0;

    while (*p && lines < 3) {
        const char *lstart = p;
        while (*p && *p != '\n') ++p;
        size_t llen = (size_t)(p - lstart);

        /* Skip includes, empty lines, braces, and boilerplate */
        int skip = (llen == 0) ||
                   (llen > 0 && (lstart[0] == '#' || lstart[0] == '}' ||
                                 strncmp(lstart, "int main", 8) == 0 ||
                                 strncmp(lstart, "    }", 5) == 0 ||
                                 strncmp(lstart, "    retur", 9) == 0));

        if (!skip && llen > 1) {
            size_t max_print = llen > 50 ? 50 : llen;
            size_t cur = strlen(tmp);
            snprintf(tmp + cur, sizeof(tmp) - cur, "%.*s\n", (int)max_print, lstart);
            lines++;
        }

        if (*p == '\n') ++p;
    }

    if (tmp[0] == '\0') {
        snprintf(tmp, sizeof(tmp), "int x = 42;");
    }

    strncpy(out, tmp, out_size - 1);
    out[out_size - 1] = '\0';
}

/* ------------------------------------------------------------------ *
 * Public API                                                         *
 * ------------------------------------------------------------------ */

void pipeline_visualize(const char *source, const char *filename)
{
    int BOX_WIDTH = 55;

    /* ---- Prepare: lex + parse + codegen ---- */
    lexer_t lexer;
    lexer_init(&lexer, source);

    parser_t parser;
    parser_init(&parser, &lexer, ".");
    program_t *prog = parse_program(&parser);

    if (parser.had_error || !prog) {
        fprintf(stderr, "=== PIPELINE VISUALIZATION ===\n");
        fprintf(stderr, "Could not parse '%s' — please fix errors first.\n", filename);
        parser_free_imports(&parser);
        ast_destroy_program(prog);
        return;
    }

    codegen_result_t cr = codegen_emit(prog);

    /* Find first meaningful source line */
    int first_line_num = 0;
    const char *first_line = find_first_source_line(source, &first_line_num);
    size_t first_len = line_length(first_line);

    /* Truncate long lines for display */
    char display_line[128];
    {
        size_t dlen = first_len > 60 ? 60 : first_len;
        memcpy(display_line, first_line, dlen);
        display_line[dlen] = '\0';
        /* Trim trailing whitespace */
        char *end = display_line + strlen(display_line);
        while (end > display_line && isspace((unsigned char)*(end - 1))) --end;
        *end = '\0';
    }

    /* Tokenize the first line */
    char token_str[1024] = "";
    int tok_count = 0;
    tokenize_line(first_line, first_len, token_str, sizeof token_str, &tok_count);

    /* Get AST description */
    const stmt_t *first_stmt = (prog->count > 0) ? prog->stmts[0] : NULL;
    char stmt_desc[256] = "";
    char expr_desc[256] = "";
    if (first_stmt) {
        describe_stmt_type(first_stmt, stmt_desc, sizeof stmt_desc);
        get_expr_detail(first_stmt, expr_desc, sizeof expr_desc);
    } else {
        snprintf(stmt_desc, sizeof stmt_desc, "(empty program)");
    }

    /* Get generated C */
    char c_lines[512] = "";
    if (cr.code) {
        get_first_c_lines(cr.code, c_lines, sizeof c_lines);
    } else {
        snprintf(c_lines, sizeof c_lines, "(codegen failed)");
    }

    /* ---- Print header ---- */
    fprintf(stderr, "\n");
    fprintf(stderr, "  ═══════════════════════════════════════════════════════\n");
    fprintf(stderr, "     PIPELINE VISUALIZATION: %s\n", filename ? filename : "source");
    fprintf(stderr, "  ═══════════════════════════════════════════════════════\n\n");

    /* ---- Panel 1: Lexer ---- */
    print_top_box("  1. LEXER  (tokenizer)", BOX_WIDTH);
    fprintf(stderr, "  | %-*s|\n", BOX_WIDTH - 3, "");
    fprintf(stderr, "  |   Reads line %d:                                        |\n", first_line_num);
    fprintf(stderr, "  |   \"%s\"                       |\n", display_line);
    fprintf(stderr, "  | %-*s|\n", BOX_WIDTH - 3, "");

    /* Print tokens, wrapping if needed */
    if (strlen(token_str) > 0) {
        fprintf(stderr, "  |   Produces %d token%s:                               |\n",
                tok_count, tok_count == 1 ? "" : "s");

        /* Split token_str into lines that fit in the box */
        size_t tok_len = strlen(token_str);
        size_t pos = 0;
        int max_per_line = BOX_WIDTH - 8;
        while (pos < tok_len) {
            size_t chunk = (tok_len - pos > (size_t)max_per_line) ? (size_t)max_per_line : (tok_len - pos);
            fprintf(stderr, "  |     %.*s%-*s|\n", (int)chunk, token_str + pos,
                    (int)(max_per_line - chunk), "");
            pos += chunk;
        }
    } else {
        fprintf(stderr, "  |   (empty line)                                       |\n");
    }

    fprintf(stderr, "  | %-*s|\n", BOX_WIDTH - 3, "");
    print_bottom_box(BOX_WIDTH);
    print_arrow();

    /* ---- Panel 2: Parser ---- */
    print_top_box("  2. PARSER  (builds AST)", BOX_WIDTH);
    fprintf(stderr, "  | %-*s|\n", BOX_WIDTH - 3, "");
    fprintf(stderr, "  |   Reads tokens and builds a tree:                      |\n");
    fprintf(stderr, "  | %-*s|\n", BOX_WIDTH - 3, "");
    fprintf(stderr, "  |   Statement type:                                      |\n");
    fprintf(stderr, "  |     %-*s|\n", BOX_WIDTH - 7, stmt_desc);

    if (strlen(expr_desc) > 0) {
        fprintf(stderr, "  | %-*s|\n", BOX_WIDTH - 3, "");
        fprintf(stderr, "  |   Expression:                                         |\n");
        int max_per_line = BOX_WIDTH - 8;
        size_t ed_len = strlen(expr_desc);
        size_t pos = 0;
        while (pos < ed_len) {
            size_t chunk = (ed_len - pos > (size_t)max_per_line) ? (size_t)max_per_line : (ed_len - pos);
            fprintf(stderr, "  |     %.*s%-*s|\n", (int)chunk, expr_desc + pos,
                    (int)(max_per_line - chunk), "");
            pos += chunk;
        }
    }

    fprintf(stderr, "  | %-*s|\n", BOX_WIDTH - 3, "");
    print_bottom_box(BOX_WIDTH);
    print_arrow();

    /* ---- Panel 3: Codegen ---- */
    print_top_box("  3. CODEGEN  (emits C)", BOX_WIDTH);
    fprintf(stderr, "  | %-*s|\n", BOX_WIDTH - 3, "");
    fprintf(stderr, "  |   Walks the AST and generates C99 code:                |\n");
    fprintf(stderr, "  | %-*s|\n", BOX_WIDTH - 3, "");

    /* Print the generated C lines */
    if (strlen(c_lines) > 0) {
        char *lines_copy = strdup(c_lines);
        char *line = lines_copy;
        char *next;
        while (line && *line) {
            next = strchr(line, '\n');
            if (next) *next = '\0';
            /* Trim trailing newline/whitespace */
            char *end_l = line + strlen(line);
            while (end_l > line && (*end_l == '\0' || *end_l == '\n')) --end_l;
            if (*end_l == '\n') *end_l = '\0';

            int max_print = BOX_WIDTH - 6;
            int llen = (int)strlen(line);
            if (llen > max_print) llen = max_print;
            fprintf(stderr, "  |     %.*s%-*s|\n", llen, line, max_print - llen, "");
            line = next ? next + 1 : NULL;
        }
        free(lines_copy);
    }

    fprintf(stderr, "  | %-*s|\n", BOX_WIDTH - 3, "");
    print_bottom_box(BOX_WIDTH);

    /* ---- Footer ---- */
    fprintf(stderr, "\n  The generated C is then compiled with clang and run.\n");
    fprintf(stderr, "  Run 'mylang emit %s' to see the full C output.\n\n",
            filename ? filename : "file.vibes");

    /* Cleanup */
    parser_free_imports(&parser);
    ast_destroy_program(prog);
    free(cr.code);
}
