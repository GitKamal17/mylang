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

    /* Python-style indentation markers emitted by the lexer's
       indentation tracker.  The parser currently skips these (block
       structure still comes from { } braces); they exist for tooling,
       debug output, and a future indentation-based block syntax. */
    TOKEN_INDENT     = 29,
    TOKEN_DEDENT     = 30,
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

/* Maximum nesting depth of the indentation stack. */
#define MYLANG_MAX_INDENT 64

typedef struct {
    const char *start;   /* beginning of the entire source             */
    const char *current; /* next character to consume                  */
    int         line;    /* current line (1-based)                     */
    int         column;  /* current column in the current line (1-based) */

    /* --- Python-style indentation tracking ---
       At the start of each logical (physical) line the leading whitespace
       is measured and compared against indent_stack (initialised to
       [0]).  A deeper line pushes its width and emits TOKEN_INDENT; a
       shallower line pops until a matching level is found, emitting one
       TOKEN_DEDENT per level popped (queued in pending_dedents).  An
       indent that matches no level is a lexer error ("inconsistent
       indentation").  Blank and comment-only lines are skipped for this
       purpose.  At EOF, enough DEDENTs are emitted to pop the stack back
       to level 0.

       Tabs-vs-spaces policy: tabs are rejected in code ENTIRELY — a tab
       in indentation OR between tokens is a hard lexer error (this
       avoids Python's classic tab/space mixing bugs).  Only tabs inside
       string literals are allowed, and they are preserved verbatim. */
    int   indent_stack[MYLANG_MAX_INDENT]; /* levels; indent_stack[0] = 0 */
    int   indent_depth;    /* number of levels currently on the stack      */
    int   indent_pending;  /* at line start: indentation not yet measured  */
    int   pending_indent;  /* 1 = the next token is TOKEN_INDENT           */
    int   pending_dedents; /* count of TOKEN_DEDENT tokens still to emit   */
    const char *pending_error; /* error message to emit as TOKEN_ERROR (or NULL) */
    int   err_line, err_col;   /* source position for that error           */
    const char *indent_pos;    /* start of the indentation whitespace      */
    int   indent_line, indent_col, indent_width; /* position + width       */
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

