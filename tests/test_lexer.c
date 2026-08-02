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
    lexer_t l; lexer_init(&l, "window");
    token_t t = lexer_next(&l);
    ASSERT_EQ(t.type, TOKEN_KEYWORD, "type");
    ASSERT_STR_EQ(t.start, "window", "value");
    ASSERT_EQ(t.line, 1, "line");
    PASS();
}

static void test_keyword_fn(void)
{
    lexer_t l; lexer_init(&l, "fn");
    token_t t = lexer_next(&l);
    ASSERT_EQ(t.type, TOKEN_KEYWORD, "type");
    ASSERT_STR_EQ(t.start, "fn", "value");
    PASS();
}

static void test_keyword_print(void)
{
    lexer_t l; lexer_init(&l, "print");
    token_t t = lexer_next(&l);
    ASSERT_EQ(t.type, TOKEN_KEYWORD, "type");
    ASSERT_STR_EQ(t.start, "print", "value");
    PASS();
}

static void test_keyword_return(void)
{
    lexer_t l; lexer_init(&l, "return");
    token_t t = lexer_next(&l);
    ASSERT_EQ(t.type, TOKEN_KEYWORD, "type");
    ASSERT_STR_EQ(t.start, "return", "value");
    PASS();
}

static void test_keyword_else(void)
{
    lexer_t l; lexer_init(&l, "else");
    token_t t = lexer_next(&l);
    ASSERT_EQ(t.type, TOKEN_KEYWORD, "type");
    ASSERT_STR_EQ(t.start, "else", "value");
    PASS();
}

static void test_keyword_sprite(void)
{
    lexer_t l; lexer_init(&l, "sprite");
    token_t t = lexer_next(&l);
    ASSERT_EQ(t.type, TOKEN_KEYWORD, "type");
    ASSERT_STR_EQ(t.start, "sprite", "value");
    PASS();
}

static void test_keyword_draw_sprite(void)
{
    lexer_t l; lexer_init(&l, "draw_sprite");
    token_t t = lexer_next(&l);
    ASSERT_EQ(t.type, TOKEN_KEYWORD, "type");
    ASSERT_STR_EQ(t.start, "draw_sprite", "value");
    PASS();
}

static void test_punctuation(void)
{
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
    lexer_t l; lexer_init(&l, "[]");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_LBRACKET, "[");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_RBRACKET, "]");
    PASS();
}

static void test_colon(void)
{
    lexer_t l; lexer_init(&l, ":");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_COLON, ":");
    PASS();
}

static void test_operators(void)
{
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
    lexer_t l; lexer_init(&l, "myVar _foo bar123");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_IDENTIFIER, "myVar");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_IDENTIFIER, "_foo");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_IDENTIFIER, "bar123");
    PASS();
}

static void test_number_integer(void)
{
    lexer_t l; lexer_init(&l, "42");
    token_t t = lexer_next(&l);
    ASSERT_EQ(t.type, TOKEN_NUMBER, "type");
    ASSERT_STR_EQ(t.start, "42", "value");
    PASS();
}

static void test_number_float(void)
{
    lexer_t l; lexer_init(&l, "3.14159");
    token_t t = lexer_next(&l);
    ASSERT_EQ(t.type, TOKEN_NUMBER, "type");
    ASSERT_EQ(t.length, 7UL, "length");
    PASS();
}

static void test_string_simple(void)
{
    lexer_t l; lexer_init(&l, "\"hello\"");
    token_t t = lexer_next(&l);
    ASSERT_EQ(t.type, TOKEN_STRING, "type");
    ASSERT_STR_EQ(t.start, "hello", "value");
    PASS();
}

static void test_unterminated_string(void)
{
    lexer_t l; lexer_init(&l, "\"oops");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_ERROR, "type");
    PASS();
}

static void test_newline(void)
{
    lexer_t l; lexer_init(&l, "\n");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_NEWLINE, "type");
    PASS();
}

static void test_comment_slash(void)
{
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
    lexer_t l; lexer_init(&l, "# this is a hash comment\ny");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_NEWLINE, "newline after hash comment");
    token_t t = lexer_next(&l);
    ASSERT_EQ(t.type, TOKEN_IDENTIFIER, "type");
    ASSERT_STR_EQ(t.start, "y", "hash comment skipped, next is y");
    PASS();
}

static void test_mixed_tokens(void)
{
    const char *src = "fn add(a, b) {\n    return a + b\n}\n";
    lexer_t l; lexer_init(&l, src);
    ASSERT_EQ(lexer_next(&l).type, TOKEN_KEYWORD, "fn");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_IDENTIFIER, "add");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_LPAREN, "(");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_IDENTIFIER, "a");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_COMMA, ",");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_IDENTIFIER, "b");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_RPAREN, ")");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_BRACE_OPEN, "{");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_NEWLINE, "newline after {");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_KEYWORD, "return");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_IDENTIFIER, "a");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_PLUS, "+");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_IDENTIFIER, "b");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_NEWLINE, "newline after b");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_BRACE_CLOSE, "}");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_NEWLINE, "newline after }");
    ASSERT_EQ(lexer_next(&l).type, TOKEN_EOF, "EOF");
    PASS();
}

static void test_window_tokens(void)
{
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
    printf("\n--- Results: %d/%d passed, %d failed ---\n",
           tests_pass, tests_run, tests_fail);
    return tests_fail > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
