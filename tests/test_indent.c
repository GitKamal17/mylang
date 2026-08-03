/* ==================================================================== *
 * test_indent.c — standalone test for the lexer's Python-style         *
 * indentation tracker (TOKEN_INDENT / TOKEN_DEDENT).                   *
 *                                                                      *
 * It feeds a small nested .vibes-shaped string through the lexer and   *
 * DUMPs the full token stream so you can visually verify INDENT/DEDENT *
 * placement, then asserts the exact placements so it also works as a   *
 * CI regression test.                                                  *
 *                                                                      *
 * Tabs-vs-spaces policy (documented): tabs are rejected in code        *
 * ENTIRELY — in indentation OR between tokens — with a clear error.    *
 * This avoids Python's classic tab/space mixing bugs.  Only tabs       *
 * inside string literals are allowed (preserved verbatim).            *
 *                                                                      *
 * Build:  cc -I src tests/test_indent.c src/lexer.c -o test_indent     *
 * Run:    ./test_indent                                                *
 * ==================================================================== */

#include "lexer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run  = 0;
static int tests_fail = 0;

/* ------------------------------------------------------------------ *
 * Visual token-stream dump                                           *
 * ------------------------------------------------------------------ */

static void dump_tokens(const char *label, const char *src)
{
    printf("--- %s ---\n", label);
    printf("    %s\n", src);

    lexer_t lexer;
    lexer_init(&lexer, src);

    int n = 0;
    for (;;) {
        token_t tok = lexer_next(&lexer);
        char text[24];

        if (tok.type == TOKEN_NEWLINE)
            snprintf(text, sizeof text, "\\n");
        else if (tok.type == TOKEN_INDENT)
            snprintf(text, sizeof text, "<%d spaces>", (int)tok.length);
        else if (tok.type == TOKEN_DEDENT)
            snprintf(text, sizeof text, "--");
        else if (tok.type == TOKEN_EOF)
            snprintf(text, sizeof text, "(end)");
        else if (tok.type == TOKEN_ERROR)
            snprintf(text, sizeof text, "%.*s", (int)tok.length, tok.start);
        else {
            size_t m = tok.length < 18 ? tok.length : 18;
            memcpy(text, tok.start, m);
            text[m] = '\0';
        }

        printf("  %2d: L%-3d %-12s %s\n", ++n, tok.line,
               token_type_name(tok.type), text);

        if (tok.type == TOKEN_EOF || tok.type == TOKEN_ERROR)
            break;
    }
    printf("\n");
}

/* ------------------------------------------------------------------ *
 * Assertion harness                                                  *
 * ------------------------------------------------------------------ */

static void test_stream(const char *label, const char *src,
                        const token_type_t *expected, size_t count)
{
    ++tests_run;
    printf("  TEST: %-44s ", label);
    fflush(stdout);

    lexer_t lexer;
    lexer_init(&lexer, src);

    for (size_t i = 0; i < count; ++i) {
        token_t tok = lexer_next(&lexer);
        if (tok.type != expected[i]) {
            printf("FAIL -- token %zu: got %s, expected %s\n", i,
                   token_type_name(tok.type), token_type_name(expected[i]));
            ++tests_fail;
            return;
        }
    }

    /* No stray tokens allowed after the expected stream. */
    token_t tail = lexer_next(&lexer);
    if (tail.type != TOKEN_EOF) {
        printf("FAIL -- unexpected trailing token %s\n",
               token_type_name(tail.type));
        ++tests_fail;
        return;
    }

    printf("PASS\n");
}

/* Expected token-type sequence for the nested sample below. */
static const token_type_t SAMPLE_EXPECTED[] = {
    TOKEN_KEYWORD,            /* window */
    TOKEN_STRING,             /* "Demo" */
    TOKEN_COMMA,
    TOKEN_NUMBER,             /* 800 */
    TOKEN_COMMA,
    TOKEN_NUMBER,             /* 600 */
    TOKEN_NEWLINE,
    TOKEN_NEWLINE,            /* blank line */
    TOKEN_KEYWORD,            /* fn */
    TOKEN_IDENTIFIER,         /* add */
    TOKEN_LPAREN,
    TOKEN_IDENTIFIER,         /* a */
    TOKEN_COMMA,
    TOKEN_IDENTIFIER,         /* b */
    TOKEN_RPAREN,
    TOKEN_COLON,              /* : */
    TOKEN_NEWLINE,
    TOKEN_INDENT,             /*     total = a + b        (push 4) */
    TOKEN_IDENTIFIER,         /* total */
    TOKEN_EQUALS,
    TOKEN_IDENTIFIER,         /* a */
    TOKEN_PLUS,
    TOKEN_IDENTIFIER,         /* b */
    TOKEN_NEWLINE,
    TOKEN_KEYWORD,            /* if */
    TOKEN_IDENTIFIER,         /* total */
    TOKEN_GREATER,
    TOKEN_NUMBER,             /* 10 */
    TOKEN_COLON,              /* : */
    TOKEN_NEWLINE,
    TOKEN_INDENT,             /*         print "big"      (push 8) */
    TOKEN_KEYWORD,            /* print */
    TOKEN_STRING,             /* "big" */
    TOKEN_NEWLINE,
    TOKEN_DEDENT,             /*     else:                (pop 8 -> 4) */
    TOKEN_KEYWORD,            /* else */
    TOKEN_COLON,              /* : */
    TOKEN_NEWLINE,
    TOKEN_INDENT,             /*         print "small"    (push 8) */
    TOKEN_KEYWORD,            /* print */
    TOKEN_STRING,             /* "small" */
    TOKEN_NEWLINE,
    TOKEN_DEDENT,             /*     return total         (pop 8 -> 4) */
    TOKEN_KEYWORD,            /* return */
    TOKEN_IDENTIFIER,         /* total */
    TOKEN_NEWLINE,
    TOKEN_NEWLINE,            /* blank line */
    TOKEN_DEDENT,             /* score = add(3, 4)        (pop 4 -> 0) */
    TOKEN_IDENTIFIER,         /* score */
    TOKEN_EQUALS,
    TOKEN_IDENTIFIER,         /* add (not a keyword) */
    TOKEN_LPAREN,
    TOKEN_NUMBER,             /* 3 */
    TOKEN_COMMA,
    TOKEN_NUMBER,             /* 4 */
    TOKEN_RPAREN,
    TOKEN_NEWLINE,
    TOKEN_EOF,
};
#define SAMPLE_EXPECTED_COUNT (sizeof SAMPLE_EXPECTED / sizeof SAMPLE_EXPECTED[0])

/* The .vibes-shaped nested sample. */
static const char *SAMPLE_SRC =
    "window \"Demo\", 800, 600\n"
    "\n"
    "fn add(a, b):\n"
    "    total = a + b\n"
    "    if total > 10:\n"
    "        print \"big\"\n"
    "    else:\n"
    "        print \"small\"\n"
    "    return total\n"
    "\n"
    "score = add(3, 4)\n";

static void test_nested_sample(void)
{
    test_stream("nested sample INDENT/DEDENT placement",
                SAMPLE_SRC, SAMPLE_EXPECTED, SAMPLE_EXPECTED_COUNT);
}

static void test_blank_and_comment_lines(void)
{
    /* Blank + comment-only lines must not disturb the indent stack. */
    static const token_type_t exp[] = {
        TOKEN_IDENTIFIER,     /* a */
        TOKEN_NEWLINE,        /* newline after a */
        TOKEN_NEWLINE,        /* (blank) */
        TOKEN_NEWLINE,        /* (comment-only) */
        TOKEN_INDENT,         /*     b   -- still exactly one INDENT */
        TOKEN_IDENTIFIER,     /* b */
        TOKEN_NEWLINE,
        TOKEN_DEDENT,         /* EOF */
        TOKEN_EOF,
    };
    test_stream("blank/comment lines skip indentation",
                "a\n\n# comment\n    b\n", exp, sizeof exp / sizeof exp[0]);
}

static void test_eof_dedent(void)
{
    static const token_type_t exp[] = {
        TOKEN_IDENTIFIER, TOKEN_NEWLINE,
        TOKEN_INDENT, TOKEN_IDENTIFIER, TOKEN_NEWLINE,
        TOKEN_DEDENT, TOKEN_EOF,
    };
    test_stream("DEDENT emitted at EOF", "a\n    b\n",
                exp, sizeof exp / sizeof exp[0]);
}

static void test_multiple_dedents_one_line(void)
{
    static const token_type_t exp[] = {
        TOKEN_IDENTIFIER, TOKEN_NEWLINE,
        TOKEN_INDENT, TOKEN_IDENTIFIER, TOKEN_NEWLINE,
        TOKEN_INDENT, TOKEN_IDENTIFIER, TOKEN_NEWLINE,
        TOKEN_DEDENT, TOKEN_DEDENT,      /* 8 -> 0 in one jump */
        TOKEN_IDENTIFIER, TOKEN_EOF,
    };
    test_stream("two DEDENTs on one line",
                "a\n    b\n        c\nx", exp, sizeof exp / sizeof exp[0]);
}

static void test_tab_rejected(void)
{
    ++tests_run;
    printf("  TEST: %-44s ", "tab in indentation -> ERROR");
    fflush(stdout);

    lexer_t l;
    lexer_init(&l, "a\n\tb");
    (void)lexer_next(&l);   /* a */
    (void)lexer_next(&l);   /* newline */
    token_t t = lexer_next(&l);
    if (t.type != TOKEN_ERROR) {
        printf("FAIL -- expected ERROR, got %s\n", token_type_name(t.type));
        ++tests_fail;
        return;
    }
    if (strncmp(t.start, "tab character in indentation", 28) != 0) {
        printf("FAIL -- wrong message: \"%.*s\"\n", (int)t.length, t.start);
        ++tests_fail;
        return;
    }
    printf("PASS\n");
}

static void test_inconsistent_indent(void)
{
    ++tests_run;
    printf("  TEST: %-44s ", "inconsistent indentation -> ERROR");
    fflush(stdout);

    lexer_t l;
    lexer_init(&l, "a\n    b\n  c");   /* 0 -> 4 -> 2: no match */
    (void)lexer_next(&l);   /* a */
    (void)lexer_next(&l);   /* newline */
    (void)lexer_next(&l);   /* INDENT(4) */
    (void)lexer_next(&l);   /* b */
    (void)lexer_next(&l);   /* newline */
    token_t t = lexer_next(&l);
    if (t.type != TOKEN_ERROR) {
        printf("FAIL -- expected ERROR, got %s\n", token_type_name(t.type));
        ++tests_fail;
        return;
    }
    if (strcmp(t.start, "inconsistent indentation") != 0) {
        printf("FAIL -- wrong message: \"%.*s\"\n", (int)t.length, t.start);
        ++tests_fail;
        return;
    }
    if (t.line != 3) {
        printf("FAIL -- error should be on line 3, got line %d\n", t.line);
        ++tests_fail;
        return;
    }
    printf("PASS\n");
}

static void test_inline_tab_rejected(void)
{
    ++tests_run;
    printf("  TEST: %-44s ", "inline tab between tokens -> ERROR");
    fflush(stdout);

    lexer_t l;
    lexer_init(&l, "a\tb");   /* tab between two identifiers */
    (void)lexer_next(&l);   /* a */
    token_t t = lexer_next(&l);
    if (t.type != TOKEN_ERROR) {
        printf("FAIL -- expected ERROR, got %s\n", token_type_name(t.type));
        ++tests_fail;
        return;
    }
    if (strncmp(t.start, "tab character in code", 21) != 0) {
        printf("FAIL -- wrong message: \"%.*s\"\n", (int)t.length, t.start);
        ++tests_fail;
        return;
    }
    printf("PASS\n");
}

static void test_tab_inside_string_ok(void)
{
    ++tests_run;
    printf("  TEST: %-44s ", "tab inside string literal is preserved");
    fflush(stdout);

    lexer_t l;
    lexer_init(&l, "\"a\tb\"");   /* tab inside a string */
    token_t t = lexer_next(&l);
    if (t.type != TOKEN_STRING) {
        printf("FAIL -- expected STRING, got %s\n", token_type_name(t.type));
        ++tests_fail;
        return;
    }
    if (t.length != 3 || strncmp(t.start, "a\tb", 3) != 0) {
        printf("FAIL -- tab not preserved inside string\n");
        ++tests_fail;
        return;
    }
    printf("PASS\n");
}

int main(void)
{
    printf("=== MyLang Indentation Tracker (lexer) ===\n\n");

    /* ---- Visual dump ---- */
    dump_tokens("nested sample (visual check)", SAMPLE_SRC);
    dump_tokens("blank + comment lines", "a\n\n# comment\n    b\n");
    dump_tokens("jump back to level 0 in one line", "a\n    b\n        c\nx");
    dump_tokens("inconsistent indentation", "a\n    b\n  c");
    dump_tokens("tab in indentation", "a\n\tb");
    dump_tokens("inline tab between tokens", "a\tb");

    /* ---- Assertions ---- */
    printf("--- Assertions ---\n");
    test_nested_sample();
    test_blank_and_comment_lines();
    test_eof_dedent();
    test_multiple_dedents_one_line();
    test_tab_rejected();
    test_inconsistent_indent();
    test_inline_tab_rejected();
    test_tab_inside_string_ok();

    printf("\n--- Results: %d/%d passed, %d failed ---\n",
           tests_run - tests_fail, tests_run, tests_fail);
    return tests_fail > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
