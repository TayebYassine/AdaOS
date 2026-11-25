#ifndef ADAOS_ADASCRIPT_INPUT_H
#define ADAOS_ADASCRIPT_INPUT_H

#include "as_env.h"

// The AdaScript "input" statement: it reads a line with the shell's editor,
// converts it to the variable's type, and checks the optional typeof and range
// limits before storing the value. Invalid input is explained and the line is
// asked for again.

/* Execute an "input" statement: read a line and validate it for a variable. */
void as_exec_input(struct as_env* env, const char* stmt);

#endif /* ADAOS_ADASCRIPT_INPUT_H */
