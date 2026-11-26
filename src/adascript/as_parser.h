#ifndef ADAOS_ADASCRIPT_PARSER_H
#define ADAOS_ADASCRIPT_PARSER_H

#include "as_env.h"

// The AdaScript expression parser: a recursive-descent parser with the usual
// precedence, from || and or down through &&, equality, comparison, addition,
// multiplication and unary operators to literals, names, array indexing and
// the built-in functions, which are called without parentheses. It evaluates
// as it parses and reports errors through the environment.

/* Evaluate an expression string. */
struct as_value as_eval_expression(struct as_env* env, const char* str);

#endif /* ADAOS_ADASCRIPT_PARSER_H */
