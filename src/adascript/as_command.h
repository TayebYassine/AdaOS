#ifndef ADAOS_ADASCRIPT_COMMAND_H
#define ADAOS_ADASCRIPT_COMMAND_H

#include "as_env.h"

// Running shell commands from AdaScript: variables written as $name are
// substituted before the command runs, an output command in an argument is
// evaluated as an expression, and its text can be used as a string value. A
// few commands that would take over the machine are refused.

/* Run an output command and hand back its text as a string value. */
int as_run_command_value(struct as_env* env, const char* command,
                         struct as_value* out);

/* Execute a built-in OS command from AdaScript with variable interpolation. */
void as_exec_system_cmd(struct as_env* env, const char* line);

#endif /* ADAOS_ADASCRIPT_COMMAND_H */
