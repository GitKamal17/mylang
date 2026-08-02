#ifndef MYLANG_LEXER_H
#define MYLANG_LEXER_H

#include <stddef.h> /* size_t */

/* ------------------------------------------------------------------ *
 * Token types recognised by the lexer.                               *
 * ------------------------------------------------------------------ */
typedef enum {
    TOKEN_EOF        = 0,
    TOKEN_ERROR      = 1,

    /* Literals */
    TOKEN_IDENTIFIER = 2,
    TOKEN_NUMBER     = 3,
    TOKEN_STRING     = 4,

    /* Keywords */
    TOKEN_KEYWORD    = 5,

    /* Punctuation */
    TOKEN_COMMA       = 6,
    TOKEN_NEWLINE     = 7,
    TOKEN_BRACE_OPEN  = 8,
    TOKEN_BRACE_CLOSE = 9,
    TOKEN_LPAREN      = 10,
    TOKEN_RPAREN      = 11,
    TOKEN_SEMICOLON   = 12,

    /* Operators */
    TOKEN_EQUALS     = 13,
    TOKEN_PLUS       = 14,
    TOKEN_MINUS      = 15,
    TOKEN_STAR       = 16,
    TOKEN_SLASH      = 17,
    TOKEN_GREATER    = 18,
    TOKEN_LESS       = 19,
    TOKEN_GREATER_EQ = 20,
    TOKEN_LESS_EQ    = 21,
    TOKEN_EQ_EQ      = 22,
    TOKEN_BANG_EQ    = 23,
    TOKEN_BANG       = 24,

    /* Brackets for array literals & index access */
    TOKEN_LBRACKET   = 25,
    TOKEN_RBRACKET   = 26,

    /* Colon for dictionary key:value pairs */
    TOKEN_COLON      = 27,

    /* Dot for method call syntax */
    TOKEN_DOT        = 28,
} token_type_t;

/* ------------------------------------------------------------------ *
 * A single token produced by the lexer.                              *
 * `start` points into the original source buffer.  The token is NOT  *
 * null-terminated; use `length` to delimit it.                       *
 * ------------------------------------------------------------------ */
typedef struct {
    token_type_t type;
    const char  *start;
    size_t       length;
    int          line;       /* 1-based line number for error messages */
    int          column;     /* 1-based column number                 */
} token_t;

/* ------------------------------------------------------------------ *
 * Lexer state.  Created on the stack, initialised with lexer_init(). *
 * ------------------------------------------------------------------ */
typedef struct {
    const char *start;   /* beginning of the entire source             */
    const char *current; /* next character to consume                  */
    int         line;    /* current line (1-based)                     */
    int         column;  /* current column in the current line (1-based) */
} lexer_t;

/* ------------------------------------------------------------------ *
 * Public API                                                         *
 * ------------------------------------------------------------------ */

void lexer_init(lexer_t *lexer, const char *source);
token_t lexer_next(lexer_t *lexer);

/**
 * token_type_name() - Return a human-readable name for a token type.
 * Used by the debug command.
 */
const char *token_type_name(token_type_t type);

/**
 * token_type_explain() - Return a plain-English explanation of what a
 * token type means in the MyLang language.  Used by --explain tokens.
 */
const char *token_type_explain(token_type_t type);

#endif /* MYLANG_LEXER_H */

