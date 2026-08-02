#include "lexer.h"

#include <ctype.h>  /* isalpha, isdigit, isspace */
#include <stdbool.h>
#include <stdio.h>   /* snprintf */
#include <string.h> /* strncmp, strlen */

/* ------------------------------------------------------------------ *
 * Forward declarations of internal helpers.                          *
 * ------------------------------------------------------------------ */
static void     skip_whitespace(lexer_t *lexer);
static void     skip_comment(lexer_t *lexer);
static token_t  make_token(lexer_t *lexer, token_type_t type);
static token_t  error_token(lexer_t *lexer, const char *message);
static bool     is_keyword(const char *start, size_t length);
static token_t  read_identifier(lexer_t *lexer);
static token_t  read_number(lexer_t *lexer);
static token_t  read_string(lexer_t *lexer);
static char     advance(lexer_t *lexer);
static char     peek(lexer_t *lexer);
static char     peek_next(lexer_t *lexer);
static bool     is_at_end(lexer_t *lexer);

/* ------------------------------------------------------------------ *
 * Public API                                                         *
 * ------------------------------------------------------------------ */

void lexer_init(lexer_t *lexer, const char *source)
{
    lexer->start   = source;
    lexer->current = source;
    lexer->line    = 1;
    lexer->column  = 1;
}

token_t lexer_next(lexer_t *lexer)
{
    skip_whitespace(lexer);

    /* Remember the start of this token (after whitespace). */
    lexer->start = lexer->current;

    if (is_at_end(lexer))
        return make_token(lexer, TOKEN_EOF);

    char c = peek(lexer);

    /* Skip comments (# and //). */
    if (c == '#') {
        skip_comment(lexer);
        return lexer_next(lexer);
    }
    if (c == '/' && peek_next(lexer) == '/') {
        skip_comment(lexer);
        return lexer_next(lexer); /* tail-call for next token */
    }

    /* Punctuation: single-character tokens */
    switch (c) {
    case ',': advance(lexer); return make_token(lexer, TOKEN_COMMA);
    case '{': advance(lexer); return make_token(lexer, TOKEN_BRACE_OPEN);
    case '}': advance(lexer); return make_token(lexer, TOKEN_BRACE_CLOSE);
    case '(': advance(lexer); return make_token(lexer, TOKEN_LPAREN);
    case ')': advance(lexer); return make_token(lexer, TOKEN_RPAREN);
    case ';': advance(lexer); return make_token(lexer, TOKEN_SEMICOLON);
    case '[': advance(lexer); return make_token(lexer, TOKEN_LBRACKET);
    case ']': advance(lexer); return make_token(lexer, TOKEN_RBRACKET);
    case ':': advance(lexer); return make_token(lexer, TOKEN_COLON);
    case '.': advance(lexer); return make_token(lexer, TOKEN_DOT);
    }

    /* Multi-char operators */
    if (c == '=' && peek_next(lexer) == '=') {
        advance(lexer); advance(lexer);
        return make_token(lexer, TOKEN_EQ_EQ);
    }
    if (c == '=') {
        advance(lexer);
        return make_token(lexer, TOKEN_EQUALS);
    }

    if (c == '!' && peek_next(lexer) == '=') {
        advance(lexer); advance(lexer);
        return make_token(lexer, TOKEN_BANG_EQ);
    }
    if (c == '!') {
        advance(lexer);
        return make_token(lexer, TOKEN_BANG);
    }

    if (c == '>' && peek_next(lexer) == '=') {
        advance(lexer); advance(lexer);
        return make_token(lexer, TOKEN_GREATER_EQ);
    }
    if (c == '>') {
        advance(lexer);
        return make_token(lexer, TOKEN_GREATER);
    }

    if (c == '<' && peek_next(lexer) == '=') {
        advance(lexer); advance(lexer);
        return make_token(lexer, TOKEN_LESS_EQ);
    }
    if (c == '<') {
        advance(lexer);
        return make_token(lexer, TOKEN_LESS);
    }

    /* Single-char operators */
    if (c == '+') { advance(lexer); return make_token(lexer, TOKEN_PLUS); }
    if (c == '-') { advance(lexer); return make_token(lexer, TOKEN_MINUS); }
    if (c == '*') { advance(lexer); return make_token(lexer, TOKEN_STAR); }
    if (c == '/') { advance(lexer); return make_token(lexer, TOKEN_SLASH); }

    /* Newlines are meaningful (statement separators). */
    if (c == '\n') {
        advance(lexer);
        token_t tok = make_token(lexer, TOKEN_NEWLINE);
        /* The newline terminates the previous line; report that line so
           errors at the end of a line point at the right source line. */
        tok.line--;
        return tok;
    }

    /* String literal */
    if (c == '"')
        return read_string(lexer);

    /* Number literal */
    if (isdigit((unsigned char)c))
        return read_number(lexer);

    /* Identifier or keyword */
    if (isalpha((unsigned char)c) || c == '_')
        return read_identifier(lexer);

    /* Anything else is an error. */
    advance(lexer);
    return error_token(lexer, "unexpected character");
}

/* ------------------------------------------------------------------ *
 * Token type names (for debug output)                                *
 * ------------------------------------------------------------------ */

const char *token_type_name(token_type_t type)
{
    switch (type) {
    case TOKEN_EOF:        return "EOF";
    case TOKEN_ERROR:      return "ERROR";
    case TOKEN_IDENTIFIER: return "IDENTIFIER";
    case TOKEN_NUMBER:     return "NUMBER";
    case TOKEN_STRING:     return "STRING";
    case TOKEN_KEYWORD:    return "KEYWORD";
    case TOKEN_COMMA:      return "COMMA";
    case TOKEN_NEWLINE:    return "NEWLINE";
    case TOKEN_BRACE_OPEN: return "BRACE_OPEN";
    case TOKEN_BRACE_CLOSE:return "BRACE_CLOSE";
    case TOKEN_LPAREN:     return "LPAREN";
    case TOKEN_RPAREN:     return "RPAREN";
    case TOKEN_SEMICOLON:  return "SEMICOLON";
    case TOKEN_EQUALS:     return "EQUALS";
    case TOKEN_PLUS:       return "PLUS";
    case TOKEN_MINUS:      return "MINUS";
    case TOKEN_STAR:       return "STAR";
    case TOKEN_SLASH:      return "SLASH";
    case TOKEN_GREATER:    return "GREATER";
    case TOKEN_LESS:       return "LESS";
    case TOKEN_GREATER_EQ: return "GREATER_EQ";
    case TOKEN_LESS_EQ:    return "LESS_EQ";
    case TOKEN_EQ_EQ:      return "EQ_EQ";
    case TOKEN_BANG_EQ:    return "BANG_EQ";
    case TOKEN_BANG:       return "BANG";
    case TOKEN_LBRACKET:   return "LBRACKET";
    case TOKEN_RBRACKET:   return "RBRACKET";
    case TOKEN_COLON:      return "COLON";
    case TOKEN_DOT:        return "DOT";
    default:               return "UNKNOWN";
    }
}

const char *token_type_explain(token_type_t type)
{
    switch (type) {
    case TOKEN_EOF:
        return "EOF marker — no more tokens in the file";
    case TOKEN_ERROR:
        return "Error token — the lexer couldn't understand this character";
    case TOKEN_IDENTIFIER:
        return "Identifier — a variable or function name (e.g. 'x', 'myVar', 'add')";
    case TOKEN_NUMBER:
        return "Number — a numeric literal (e.g. 42, 3.14)";
    case TOKEN_STRING:
        return "String — a text literal inside double quotes (e.g. \"hello\")";
    case TOKEN_KEYWORD:
        return "Keyword — a reserved word like 'if', 'while', 'print', 'fn'";
    case TOKEN_COMMA:
        return "Comma — separates items in a list (e.g. function arguments)";
    case TOKEN_NEWLINE:
        return "Newline — ends a statement or separates lines of code";
    case TOKEN_BRACE_OPEN:
        return "{ — opens a block of code (e.g. if/while/function body)";
    case TOKEN_BRACE_CLOSE:
        return "} — closes a block of code";
    case TOKEN_LPAREN:
        return "( — opens a function call or expression grouping";
    case TOKEN_RPAREN:
        return ") — closes a function call or expression grouping";
    case TOKEN_SEMICOLON:
        return "Semicolon — ends a statement (optional in MyLang)";
    case TOKEN_EQUALS:
        return "= — Assignment operator. Stores a value in a variable";
    case TOKEN_PLUS:
        return "+ — Addition operator. Adds two numbers or concatenates strings";
    case TOKEN_MINUS:
        return "- — Subtraction operator. Subtracts one number from another";
    case TOKEN_STAR:
        return "* — Multiplication operator. Multiplies two numbers";
    case TOKEN_SLASH:
        return "/ — Division operator. Divides one number by another";
    case TOKEN_GREATER:
        return "> — Greater-than comparison. Checks if left > right";
    case TOKEN_LESS:
        return "< — Less-than comparison. Checks if left < right";
    case TOKEN_GREATER_EQ:
        return ">= — Greater-than-or-equal comparison";
    case TOKEN_LESS_EQ:
        return "<= — Less-than-or-equal comparison";
    case TOKEN_EQ_EQ:
        return "== — Equality comparison. Checks if two values are equal";
    case TOKEN_BANG_EQ:
        return "!= — Not-equal comparison. Checks if two values differ";
    case TOKEN_BANG:
        return "! — Logical NOT. Inverts a boolean value";
    case TOKEN_LBRACKET:
        return "[ — Opens an array literal (e.g. [1, 2, 3]) or index access (arr[0])";
    case TOKEN_RBRACKET:
        return "] — Closes an array literal or index access";
    case TOKEN_COLON:
        return ": — Separates keys from values in a dictionary (e.g. {\"key\": value})";
    case TOKEN_DOT:
        return ". — Accesses a field or method on an object (e.g. obj.field, str.len)";
    default:
        return "Unknown token type";
    }
}

/* ------------------------------------------------------------------ *
 * Keyword table.  MyLang keywords are matched case-sensitively.      *
 * ------------------------------------------------------------------ */
static const char *keywords[] = {
    "break",
    "circle",
    "continue",
    "draw_circle",
    "draw_rect",
    "draw_sprite",
    "else",
    "fn",
    "for",
    "if",
    "import",
    "key_down",
    "key_left",
    "key_right",
    "key_up",
    "on",
    "print",
    "return",
    "sprite",
    "struct",
    "while",
    "window",
};

#define KEYWORD_COUNT (sizeof keywords / sizeof keywords[0])

static bool is_keyword(const char *start, size_t length)
{
    for (size_t i = 0; i < KEYWORD_COUNT; ++i) {
        const char *kw = keywords[i];
        size_t kwlen = strlen(kw);
        if (length == kwlen && strncmp(start, kw, kwlen) == 0)
            return true;
    }
    return false;
}

/* ------------------------------------------------------------------ *
 * Token construction helpers.                                        *
 * ------------------------------------------------------------------ */

static token_t make_token(lexer_t *lexer, token_type_t type)
{
    token_t tok;
    tok.type   = type;
    tok.start  = lexer->start;
    tok.length = (size_t)(lexer->current - lexer->start);
    tok.line   = lexer->line;
    tok.column = lexer->column - (int)(lexer->current - lexer->start);
    return tok;
}

static token_t error_token(lexer_t *lexer, const char *message)
{
    token_t tok;
    tok.type   = TOKEN_ERROR;
    tok.start  = message;
    tok.length = strlen(message);
    tok.line   = lexer->line;
    tok.column = lexer->column;
    return tok;
}

/* ------------------------------------------------------------------ *
 * Lexer internals                                                    *
 * ------------------------------------------------------------------ */

static char advance(lexer_t *lexer)
{
    char c = *lexer->current;
    ++lexer->current;
    if (c == '\n') {
        ++lexer->line;
        lexer->column = 1;
    } else {
        ++lexer->column;
    }
    return c;
}

static char peek(lexer_t *lexer)
{
    return *lexer->current;
}

static char peek_next(lexer_t *lexer)
{
    if (is_at_end(lexer))
        return '\0';
    return *(lexer->current + 1);
}

static bool is_at_end(lexer_t *lexer)
{
    return *lexer->current == '\0';
}

static void skip_whitespace(lexer_t *lexer)
{
    for (;;) {
        char c = peek(lexer);
        switch (c) {
        case ' ':
        case '\t':
        case '\r':
            advance(lexer);
            break;
        default:
            return;
        }
    }
}

static void skip_comment(lexer_t *lexer)
{
    /* consume the two '/' characters (started by peek/peek_next) */
    advance(lexer); /* '/' */
    advance(lexer); /* '/' */
    while (!is_at_end(lexer) && peek(lexer) != '\n')
        advance(lexer);
}

/* ------------------------------------------------------------------ *
 * Token readers                                                      *
 * ------------------------------------------------------------------ */

static token_t read_identifier(lexer_t *lexer)
{
    while (isalpha((unsigned char)peek(lexer)) ||
           peek(lexer) == '_'                     ||
           isdigit((unsigned char)peek(lexer)))
    {
        advance(lexer);
    }

    size_t len = (size_t)(lexer->current - lexer->start);
    token_type_t type = is_keyword(lexer->start, len)
                            ? TOKEN_KEYWORD
                            : TOKEN_IDENTIFIER;
    return make_token(lexer, type);
}

static token_t read_number(lexer_t *lexer)
{
    while (isdigit((unsigned char)peek(lexer)))
        advance(lexer);

    if (peek(lexer) == '.') {
        advance(lexer);
        while (isdigit((unsigned char)peek(lexer)))
            advance(lexer);
    }

    return make_token(lexer, TOKEN_NUMBER);
}

static token_t read_string(lexer_t *lexer)
{
    advance(lexer);
    lexer->start = lexer->current;

    while (!is_at_end(lexer) && peek(lexer) != '"') {
        if (peek(lexer) == '\\')
            advance(lexer);
        advance(lexer);
    }

    size_t len = (size_t)(lexer->current - lexer->start);

    if (is_at_end(lexer))
        return error_token(lexer, "unterminated string literal");

    advance(lexer);

    token_t tok = make_token(lexer, TOKEN_STRING);
    tok.length  = len;
    return tok;
}
