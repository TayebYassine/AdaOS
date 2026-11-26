#ifndef ADAOS_ADASCRIPT_FUNCTIONS_H
#define ADAOS_ADASCRIPT_FUNCTIONS_H

#include "as_env.h"

// The built-in functions an expression can call without parentheses: the name
// comes first and then its arguments, as in `lengthof name` or
// `replace text "a" "b"`. The parser evaluates the arguments and hands them
// here, so this module only deals with values and errors.

/* Most arguments one call takes. */
#define AS_FUNCTION_MAX_ARGS 3

/* How many arguments the named function takes, or -1 when it is not one. */
int as_function_arity(const char* name);

/* Run a function on its evaluated arguments and return its result. */
struct as_value as_function_call(struct as_env* env, const char* name,
                                 struct as_value* args, int argc);

#endif /* ADAOS_ADASCRIPT_FUNCTIONS_H */
