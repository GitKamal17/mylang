#ifndef MYLANG_PIPELINE_H
#define MYLANG_PIPELINE_H

/**
 * pipeline_visualize() - Print a three-panel diagram to stderr showing
 * how a MyLang source file passes through the lexer, parser, and codegen.
 * `source` is the full source string, `filename` is used for display.
 */
void pipeline_visualize(const char *source, const char *filename);

#endif /* MYLANG_PIPELINE_H */
