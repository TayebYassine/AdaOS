#ifndef ADAOS_ADASCRIPT_ERROR_H
#define ADAOS_ADASCRIPT_ERROR_H

#include "as_env.h"

// Error reporting for AdaScript: one place that turns a formatted message into
// the environment's error state, prints it with the line and column of the
// failing statement, and turns a command's captured "Error:" line into a
// script error.

/* Stop the script on an error, showing where it happened. */
void as_errorf(struct as_env* env, const char* fmt, ...);

/* Turn a command's captured "Error: ..." line into a script error. */
void as_error_from_command(struct as_env* env, const char* output);

#endif /* ADAOS_ADASCRIPT_ERROR_H */
