#include "lexer.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int  tests_run   = 0;
static int  tests_pass  = 0;
static int  tests_fail  = 0;

#define TEST(name)                                  \
    do {                                            \
        ++tests_run;                                \
        printf("  TEST: %-40s ", name);             \
        fflush(stdout);                             \
    } while (0)

#define PASS()                                      \
    do {                                            \
        ++tests_pass;                               \
        printf("PASS\n");                           \
    } while (0)

#define FAIL(msg)                                   \
    do {                                            \
        ++tests_fail;                               \
        printf("FAIL -- %s\n", msg);                \
    } while (0)

#define ASSERT_EQ(actual, expected, field)          \
    do {                                            \
        if ((actual) != (expected)) {               \
            char buf[256];                          \
            snprintf(buf, sizeof buf,               \
                     "%s: got %d, expected %d",     \
                     (field), (int)(actual), (int)(expected)); \
            FAIL(buf);                              \
            return;                                 \
        }                                           \
    } while (0)

#define ASSERT_STR_EQ(s1, s2, field)                \
    do {                                            \
        if (strncmp((s1), (s2), strlen(s2)) != 0) { \
            char buf[256];                          \
            snprintf(buf, sizeof buf,               \
                     "%s: got \"%.*s\", expected \"%s\"", \
                     (field),                       \
                     (int)strlen(s2), (s1), (s2));  \
            FAIL(buf);                              \
            return;                                 \
        }                                           \
    } while (0)

static void test_empty_source(void)
{
    TEST("empty source");
    lexer_t l;
    lexer_init(&l, "");
    token_t t = lexer_next(&l);
    ASSERT_EQ(t.type, TOKEN_EOF, "type");
    t = lexer_next(&l);
    ASSERT_EQ(t.type, TOKEN_EOF, "type (2nd)");
    PASS();
}

static void test_keyword_window(void)
{
    TEST("keyword: window");
    lexer_t l; lexer_init(&l, "window");
    token_t t = lexer_next(&l);
    ASSERT_EQ(t.type, TOKEN_KEYWORD, "type");
    ASSERT_STR_EQ(t.start, "window", "value");
    ASSERT_EQ(t.line, 1, "line");
    PASS();
}

static void test_keyword_fn(void)
{
    TEST("keyword: fn");
    lexer_t l; lexer_init(&l, "fn");
    token_t t = lexer_next(&l);
    ASSERT_EQ(t.type, TOKEN_KEYWORD, "type");
    ASSERT_STR_EQ(t.start, "fn", "value");
    PASS();
}

static void test_keyword_print(void)
{
    TEST("keyword: print");
    lexer_t l; lexer_init(&l, "print");
    token_t t = lexer_next(&l);
    ASSERT_EQ(t.type, TOKEN_KEYWORD, "type");
    ASSERT_STR_EQ(t.start, "print", "value");
    PASS();
}

static void test_keyword_return(void)
{
    TEST("keyword: return");
    lexer_t l; lexer_init(&l, "return");
    token_t t = lexer_next(&l);
    ASSERT_EQ(t.type, TOKEN_KEYWORD, "type");
    ASSERT_STR_EQ(t.start, "return", "value");
    PASS();
}

static void test_keyword_else(void)
{
    TEST("keyword: else");
    lexer_t l; lexer_init(&l, "else");
    token_t t = lexer_next(&l);
    ASSERT_EQ(t.type, TOKEN_KEYWORD, "type");
    ASSERT_STR_EQ(t.start, "else", "value");
    PASS();
}

static void test_keyword_sprite(void)
{
    TEST("keyword: sprite");
    lexer_t l; lexer_init(&l, "sprite");
    token_t t = lexer_next(&l);
    ASSERT_EQ(t.type, TOKEN_KEYWORD, "type");
    ASSERT_STR_EQ(t.start, "sprite", "value");
    PASS();
}

static void test_keyword_draw_sprite(void)
{
    TEST("keyword: draw_sprite");
    lexer_t l; lexer_init(&l, "draw_sprite");
    token_t t = lexer_next(&l);
    ASSERT_EQ(t.type, TOKEN_KEYWORD, "type");
    ASSERT_STR_EQ(t.start, "draw_sprite", "value");
    PASS();
}

static void test_punctuation(void)
{
    TEST("punctuation tokens");
    lexer_t l; lexer_init(&l, "(){};,,");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_LPAREN, "(");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_RPAREN, ")");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_BRACE_OPEN, "{");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_BRACE_CLOSE, "}");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_SEMICOLON, ";");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_COMMA, ",");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_COMMA, ",");
    PASS();
}

static void test_brackets(void)
{
    TEST("brackets");
    lexer_t l; lexer_init(&l, "[]");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_LBRACKET, "[");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_RBRACKET, "]");
    PASS();
}

static void test_colon(void)
{
    TEST("colon");
    lexer_t l; lexer_init(&l, ":");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_COLON, ":");
    PASS();
}

static void test_operators(void)
{
    TEST("operators");
    lexer_t l; lexer_init(&l, "= + - * / !");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_EQUALS, "=");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_PLUS, "+");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_MINUS, "-");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_STAR, "*");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_SLASH, "/");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_BANG, "!");
    PASS();
}

static void test_comparison_ops(void)
{
    TEST("comparison operators");
    lexer_t l; lexer_init(&l, "> < >= <= == !=");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_GREATER, ">");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_LESS, "<");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_GREATER_EQ, ">=");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_LESS_EQ, "<=");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_EQ_EQ, "==");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_BANG_EQ, "!=");
    PASS();
}

static void test_identifier(void)
{
    TEST("identifier");
    lexer_t l; lexer_init(&l, "myVar _foo bar123");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_IDENTIFIER, "myVar");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_IDENTIFIER, "_foo");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_IDENTIFIER, "bar123");
    PASS();
}

static void test_number_integer(void)
{
    TEST("number: integer");
    lexer_t l; lexer_init(&l, "42");
    token_t t = lexer_next(&l);
    ASSERT_EQ(t.type, TOKEN_NUMBER, "type");
    ASSERT_STR_EQ(t.start, "42", "value");
    PASS();
}

static void test_number_float(void)
{
    TEST("number: float");
    lexer_t l; lexer_init(&l, "3.14159");
    token_t t = lexer_next(&l);
    ASSERT_EQ(t.type, TOKEN_NUMBER, "type");
    ASSERT_EQ(t.length, 7UL, "length");
    PASS();
}

static void test_string_simple(void)
{
    TEST("string literal");
    lexer_t l; lexer_init(&l, "\"hello\"");
    token_t t = lexer_next(&l);
    ASSERT_EQ(t.type, TOKEN_STRING, "type");
    ASSERT_STR_EQ(t.start, "hello", "value");
    PASS();
}

static void test_unterminated_string(void)
{
    TEST("unterminated string -> ERROR");
    lexer_t l; lexer_init(&l, "\"oops");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_ERROR, "type");
    PASS();
}

static void test_newline(void)
{
    TEST("newline token");
    lexer_t l; lexer_init(&l, "\n");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_NEWLINE, "type");
    PASS();
}

static void test_comment_slash(void)
{
    TEST("// comment");
    lexer_t l; lexer_init(&l, "// this is a comment\nx");
    /* Comment is consumed, then newline is emitted, then x */
    ASSERT_EQ(lexer_next(&l).type, TOKEN_NEWLINE, "newline after comment");
    token_t t = lexer_next(&l);
    ASSERT_EQ(t.type, TOKEN_IDENTIFIER, "type");
    ASSERT_STR_EQ(t.start, "x", "comment skipped, next is x");
    PASS();
}

static void test_comment_hash(void)
{
    TEST("# comment");
    lexer_t l; lexer_init(&l, "# this is a hash comment\ny");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_NEWLINE, "newline after hash comment");
    token_t t = lexer_next(&l);
    ASSERT_EQ(t.type, TOKEN_IDENTIFIER, "type");
    ASSERT_STR_EQ(t.start, "y", "hash comment skipped, next is y");
    PASS();
}

static void test_mixed_tokens(void)
{
    TEST("mixed tokens (colon syntax)");
    const char *src = "fn add(a, b):\n    return a + b\n";
    lexer_t l; lexer_init(&l, src);
    ASSERT_EQ(lexer_next(&l).type, TOKEN_KEYWORD, "fn");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_IDENTIFIER, "add");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_LPAREN, "(");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_IDENTIFIER, "a");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_COMMA, ",");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_IDENTIFIER, "b");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_RPAREN, ")");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_COLON, ":");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_NEWLINE, "newline after :");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_INDENT, "INDENT before return");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_KEYWORD, "return");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_IDENTIFIER, "a");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_PLUS, "+");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_IDENTIFIER, "b");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_NEWLINE, "newline after b");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_DEDENT, "DEDENT at EOF");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_EOF, "EOF");
    PASS();
}

/* ---- Python-style indentation tracking ---- */

static void test_indent_basic(void)
{
    TEST("indent: basic");
    /* a → INDENT(4) → b → DEDENT → c */
    lexer_t l; lexer_init(&l, "a\n    b\nc");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_IDENTIFIER, "a");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_NEWLINE, "newline");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_INDENT, "INDENT before b");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_IDENTIFIER, "b");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_NEWLINE, "newline after b");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_DEDENT, "DEDENT before c");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_IDENTIFIER, "c");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_EOF, "EOF");
    PASS();
}

static void test_indent_nested(void)
{
    TEST("indent: nested");
    /* 0 → 4 → 8 → 4 → 0 : two INDENTs, two DEDENTs */
    const char *src = "a\n    b\n        c\n    d\ne";
    lexer_t l; lexer_init(&l, src);
    ASSERT_EQ(lexer_next(&l).type, TOKEN_IDENTIFIER, "a");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_NEWLINE, "nl");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_INDENT, "INDENT(4)");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_IDENTIFIER, "b");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_NEWLINE, "nl");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_INDENT, "INDENT(8)");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_IDENTIFIER, "c");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_NEWLINE, "nl");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_DEDENT, "DEDENT to 4");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_IDENTIFIER, "d");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_NEWLINE, "nl");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_DEDENT, "DEDENT to 0");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_IDENTIFIER, "e");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_EOF, "EOF");
    PASS();
}

static void test_indent_multiple_dedents_one_line(void)
{
    TEST("indent: multiple DEDENTs one line");
    /* Jumping straight from 8 back to 0 emits TWO DEDENTs on one line. */
    const char *src = "a\n    b\n        c\nx";
    lexer_t l; lexer_init(&l, src);
    ASSERT_EQ(lexer_next(&l).type, TOKEN_IDENTIFIER, "a");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_NEWLINE, "nl");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_INDENT, "INDENT(4)");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_IDENTIFIER, "b");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_NEWLINE, "nl");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_INDENT, "INDENT(8)");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_IDENTIFIER, "c");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_NEWLINE, "nl");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_DEDENT, "DEDENT #1");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_DEDENT, "DEDENT #2");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_IDENTIFIER, "x");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_EOF, "EOF");
    PASS();
}

static void test_indent_same_level(void)
{
    TEST("indent: same level");
    /* Consecutive lines at the same deeper level emit no extra tokens. */
    const char *src = "a\n    b\n    c\n";
    lexer_t l; lexer_init(&l, src);
    ASSERT_EQ(lexer_next(&l).type, TOKEN_IDENTIFIER, "a");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_NEWLINE, "nl");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_INDENT, "INDENT");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_IDENTIFIER, "b");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_NEWLINE, "nl");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_IDENTIFIER, "c");   /* no token: same level */
    ASSERT_EQ(lexer_next(&l).type, TOKEN_NEWLINE, "nl");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_DEDENT, "DEDENT at EOF");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_EOF, "EOF");
    PASS();
}

static void test_indent_blank_and_comment_lines(void)
{
    TEST("indent: blank/comment lines");
    /* Blank and comment-only lines are skipped for indentation: the
       first real indented line still emits exactly one INDENT, and the
       newlines for the skipped lines are preserved. */
    const char *src = "a\n\n    # comment\n    b\n";
    lexer_t l; lexer_init(&l, src);
    ASSERT_EQ(lexer_next(&l).type, TOKEN_IDENTIFIER, "a");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_NEWLINE, "nl after a");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_NEWLINE, "blank line");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_NEWLINE, "comment-only line");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_INDENT, "INDENT still exactly one");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_IDENTIFIER, "b");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_NEWLINE, "nl after b");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_DEDENT, "DEDENT at EOF");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_EOF, "EOF");
    PASS();
}

static void test_indent_eof_dedent(void)
{
    TEST("indent: DEDENT at EOF");
    /* No trailing newline: the DEDENT still arrives before EOF. */
    lexer_t l; lexer_init(&l, "a\n    b");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_IDENTIFIER, "a");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_NEWLINE, "nl");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_INDENT, "INDENT");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_IDENTIFIER, "b");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_DEDENT, "DEDENT before EOF");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_EOF, "EOF");
    PASS();
}

static void test_indent_tab_rejected(void)
{
    TEST("indent: tab rejected");
    /* Tabs are rejected in indentation with a clear error message. */
    lexer_t l; lexer_init(&l, "a\n\tb");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_IDENTIFIER, "a");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_NEWLINE, "nl");
    token_t t = lexer_next(&l);
    ASSERT_EQ(t.type, TOKEN_ERROR, "tab in indentation -> ERROR");
    ASSERT_STR_EQ(t.start, "tab character in indentation", "error message prefix");
    ASSERT_EQ(t.line, 2, "error on line 2");
    PASS();
}

static void test_indent_inconsistent(void)
{
    TEST("indent: inconsistent -> ERROR");
    /* 0 → 4 → 2: 2 matches no level on the stack [0, 4]. */
    lexer_t l; lexer_init(&l, "a\n    b\n  c");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_IDENTIFIER, "a");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_NEWLINE, "nl");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_INDENT, "INDENT(4)");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_IDENTIFIER, "b");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_NEWLINE, "nl");
    token_t t = lexer_next(&l);
    ASSERT_EQ(t.type, TOKEN_ERROR, "inconsistent indentation -> ERROR");
    ASSERT_STR_EQ(t.start, "inconsistent indentation", "error message");
    ASSERT_EQ(t.line, 3, "error on line 3");
    PASS();
}

static void test_inline_tab_rejected(void)
{
    TEST("inline tab rejected");
    /* Tabs between tokens are rejected too (policy: tabs in code). */
    lexer_t l; lexer_init(&l, "a\tb");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_IDENTIFIER, "a");
    token_t t = lexer_next(&l);
    ASSERT_EQ(t.type, TOKEN_ERROR, "inline tab -> ERROR");
    ASSERT_STR_EQ(t.start, "tab character in code", "error message prefix");
    PASS();
}

static void test_tab_inside_string_ok(void)
{
    TEST("tab inside string preserved");
    /* Tabs inside string literals are preserved, not rejected. */
    lexer_t l; lexer_init(&l, "\"a\tb\"");
    token_t t = lexer_next(&l);
    ASSERT_EQ(t.type, TOKEN_STRING, "string");
    ASSERT_EQ(t.length, 3UL, "length includes tab");
    ASSERT_STR_EQ(t.start, "a\tb", "tab preserved");
    PASS();
}

static void test_window_tokens(void)
{
    TEST("window statement tokens");
    lexer_t l; lexer_init(&l, "window \"My Game\", 800, 600\n");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_KEYWORD, "window");
    token_t t = lexer_next(&l);
    ASSERT_EQ(t.type, TOKEN_STRING, "string");
    ASSERT_STR_EQ(t.start, "My Game", "title");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_COMMA, "comma");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_NUMBER, "800");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_COMMA, "comma");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_NUMBER, "600");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_NEWLINE, "newline");
    PASS();
}

int main(void)
{
    printf("=== MyLang Lexer Test Suite ===\n\n");
    test_empty_source();
    test_keyword_window();
    test_keyword_fn();
    test_keyword_print();
    test_keyword_return();
    test_keyword_else();
    test_keyword_sprite();
    test_keyword_draw_sprite();
    test_punctuation();
    test_brackets();
    test_colon();
    test_operators();
    test_comparison_ops();
    test_identifier();
    test_number_integer();
    test_number_float();
    test_string_simple();
    test_unterminated_string();
    test_newline();
    test_comment_slash();
    test_comment_hash();
    test_mixed_tokens();
    test_window_tokens();
    test_indent_basic();
    test_indent_nested();
    test_indent_multiple_dedents_one_line();
    test_indent_same_level();
    test_indent_blank_and_comment_lines();
    test_indent_eof_dedent();
    test_indent_tab_rejected();
    test_indent_inconsistent();
    test_inline_tab_rejected();
    test_tab_inside_string_ok();
    printf("\n--- Results: %d/%d passed, %d failed ---\n",
           tests_pass, tests_run, tests_fail);
    return tests_fail > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
