#ifndef ADAOS_ADASCRIPT_INTERP_H
#define ADAOS_ADASCRIPT_INTERP_H

#include "as_env.h"

// The AdaScript statement interpreter: it walks the prepared lines, groups
// them into for, while and if blocks, and runs the simple statements - define,
// delete, print, input, mode, writecell, erasecell, assignment and shell
// commands. Break, continue, return and errors travel through the environment
// flags.

/* One parsed line in the script. */
struct as_line {
    char* text;     // the line, leading spaces removed
    int orig_line;  // line number in the original file
    int orig_col;   // column the text starts at
    int indent;     // leading spaces, used to build blocks
};

/* Execute a block of lines between start and end (inclusive). */
int as_exec_block(struct as_env* env, struct as_line* lines, int start,
                  int end);

/* Check every statement against the mode the script declared. Returns 0 when
 * the script may run, -1 after reporting the first statement used in the
 * wrong mode. */
int as_check_script_mode(struct as_env* env, struct as_line* lines, int total);

#endif /* ADAOS_ADASCRIPT_INTERP_H */
