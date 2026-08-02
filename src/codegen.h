#ifndef MYLANG_CODEGEN_H
#define MYLANG_CODEGEN_H

#include "ast.h"

/**
 * codegen_result_t - Result of code generation.
 * code: heap-allocated C source string (caller must free).
 * needs_raylib: non-zero if the emitted code uses Raylib (game mode).
 */
typedef struct {
    char *code;
    int   needs_raylib;
} codegen_result_t;

/**
 * codegen_emit() - Walk the AST and produce a complete C source file.
 * Returns a codegen_result_t.  The caller must free() result.code.
 * If result.code is NULL, an error occurred.
 */
codegen_result_t codegen_emit(const program_t *prog);

/**
 * codegen_emit_explain() - Like codegen_emit, but adds // [file:line]
 * annotations to every statement in the generated C code.
 * Used by the --explain emit command.
 */
codegen_result_t codegen_emit_explain(const program_t *prog,
                                       const char *filename);

#endif /* MYLANG_CODEGEN_H */

