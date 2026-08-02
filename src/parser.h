#ifndef MYLANG_PARSER_H
#define MYLANG_PARSER_H

#include "ast.h"
#include "lexer.h"

/* ------------------------------------------------------------------ *
 * Parser state.  Users create one on the stack, initialise it with   *
 * parser_init(), then call parse_program() to get an AST.            *
 * ------------------------------------------------------------------ */
typedef struct {
    lexer_t *lexer;
    token_t  previous;   /* most recently consumed token               */
    token_t  current;    /* look-ahead token (peek)                    */
    int      had_error;  /* non-zero if a parse error was reported     */
    int      panicking;  /* recovering from an error in this statement */

    /* Import support */
    char      base_dir[1024];   /* directory of the current source file */
    char    **import_stack;     /* resolved paths currently being parsed  */
    size_t    import_count;
    size_t    import_cap;

    /* Loop depth tracking (for break/continue validation) */
    int       loop_depth;
} parser_t;

/* ------------------------------------------------------------------ *
 * Public API                                                         *
 * ------------------------------------------------------------------ */

/**
 * parser_init() - Initialise a parser to consume tokens from `lexer`.
 * base_dir is the directory of the source file (for resolving imports).
 */
void parser_init(parser_t *parser, lexer_t *lexer, const char *base_dir);

/**
 * parse_program() - Parse the entire token stream into a program AST.
 * Returns a heap-allocated program.  The caller must free it with
 * ast_destroy_program().
 */
program_t *parse_program(parser_t *parser);

/**
 * parser_free_imports() - Free the import stack (call after parsing).
 */
void parser_free_imports(parser_t *parser);

/**
 * ast_debug_print() - Print a human-readable representation of the AST
 * to stderr.  Used by the debug command.
 */
void ast_debug_print(const program_t *prog);

/**
 * ast_debug_print_stmt() - Print a single statement subtree to stderr.
 * Used by the step command to walk the AST node by node.
 */
void ast_debug_print_stmt(const stmt_t *s);

#endif /* MYLANG_PARSER_H */
