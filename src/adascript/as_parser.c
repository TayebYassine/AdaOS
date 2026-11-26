#include "as_parser.h"

#include "as_command.h"
#include "as_env.h"
#include "as_error.h"
#include "as_functions.h"
#include "as_lexer.h"
#include "as_value.h"
#include "command.h"
#include "kmalloc.h"
#include "libc.h"

static struct as_value as_parse_or(struct as_env* env, struct as_lexer* lex);
static struct as_value as_parse_and(struct as_env* env, struct as_lexer* lex);
static struct as_value as_parse_equality(struct as_env* env,
                                         struct as_lexer* lex);
static struct as_value as_parse_relational(struct as_env* env,
                                           struct as_lexer* lex);
static struct as_value as_parse_add(struct as_env* env, struct as_lexer* lex);
static struct as_value as_parse_mul(struct as_env* env, struct as_lexer* lex);
static struct as_value as_parse_unary(struct as_env* env, struct as_lexer* lex);
static struct as_value as_parse_primary(struct as_env* env,
                                        struct as_lexer* lex);

// Function calls, brackets, arrays and the unary operators all parse
// themselves recursively, and every level costs a whole chain of stack frames.
// A single line of script can therefore nest deep enough to run off the 16 KiB
// task stack, so the parser counts its levels and stops before that happens.
#define AS_MAX_NESTING 6

/* Claim one nesting level; 0 (with the error set) when it is too deep. */
static int as_nest(struct as_env* env, struct as_lexer* lex) {
    if (lex->depth >= AS_MAX_NESTING) {
        as_errorf(env, "the expression nests deeper than %d levels.",
                  AS_MAX_NESTING);
        return 0;
    }

    lex->depth++;
    return 1;
}

/* The value a parser hands back once it has given up; the error is already
 * set, so every caller checks env->error before using it. */
static struct as_value as_error_value(void) {
    struct as_value val;

    as_value_init(&val, AS_TYPE_INT32, AS_TYPE_VOID);
    val.is_null = 1;

    return val;
}

/* Lowest precedence: || and or. */
static struct as_value as_parse_or(struct as_env* env, struct as_lexer* lex) {
    struct as_value left = as_parse_and(env, lex);
    while (lex->cur.kind == TOK_OR) {
        as_lexer_next(lex);
        struct as_value right = as_parse_and(env, lex);
        int res = as_value_is_truthy(&left) || as_value_is_truthy(&right);
        as_value_free(&left);
        as_value_free(&right);
        left.type = AS_TYPE_BOOL;
        left.is_null = 0;
        left.u.boolean = res;
    }
    return left;
}

/* && and and, one step tighter than ||. */
static struct as_value as_parse_and(struct as_env* env, struct as_lexer* lex) {
    struct as_value left = as_parse_equality(env, lex);
    while (lex->cur.kind == TOK_AND) {
        as_lexer_next(lex);
        struct as_value right = as_parse_equality(env, lex);
        int res = as_value_is_truthy(&left) && as_value_is_truthy(&right);
        as_value_free(&left);
        as_value_free(&right);
        left.type = AS_TYPE_BOOL;
        left.is_null = 0;
        left.u.boolean = res;
    }
    return left;
}

static struct as_value as_parse_equality(struct as_env* env,
                                         struct as_lexer* lex) {
    struct as_value left = as_parse_relational(env, lex);
    while (lex->cur.kind == TOK_EQ || lex->cur.kind == TOK_NEQ) {
        enum as_tok_kind op = lex->cur.kind;
        as_lexer_next(lex);
        struct as_value right = as_parse_relational(env, lex);
        int eq = 0;

        if (left.is_null || right.is_null) {
            eq = (left.is_null && right.is_null);
        } else if (left.type == AS_TYPE_STRING &&
                   right.type == AS_TYPE_STRING) {
            eq = (left.u.str != 0 && right.u.str != 0 &&
                  strcmp(left.u.str, right.u.str) == 0);
        } else if (left.type == AS_TYPE_BOOL && right.type == AS_TYPE_BOOL) {
            eq = (left.u.boolean == right.u.boolean);
        } else {
            eq = (as_value_to_i32(&left) == as_value_to_i32(&right));
        }

        int res = (op == TOK_EQ) ? eq : !eq;
        as_value_free(&left);
        as_value_free(&right);
        left.type = AS_TYPE_BOOL;
        left.is_null = 0;
        left.u.boolean = res;
    }
    return left;
}

static struct as_value as_parse_relational(struct as_env* env,
                                           struct as_lexer* lex) {
    struct as_value left = as_parse_add(env, lex);
    while (lex->cur.kind == TOK_LT || lex->cur.kind == TOK_LTE ||
           lex->cur.kind == TOK_GT || lex->cur.kind == TOK_GTE) {
        enum as_tok_kind op = lex->cur.kind;
        as_lexer_next(lex);
        struct as_value right = as_parse_add(env, lex);
        int cmp = 0;

        if (left.type == AS_TYPE_STRING && right.type == AS_TYPE_STRING) {
            cmp = strcmp(left.u.str ? left.u.str : "",
                         right.u.str ? right.u.str : "");
        } else {
            int32_t a = as_value_to_i32(&left);
            int32_t b = as_value_to_i32(&right);
            cmp = (a < b) ? -1 : (a > b ? 1 : 0);
        }

        int res = 0;
        if (op == TOK_LT)
            res = (cmp < 0);
        else if (op == TOK_LTE)
            res = (cmp <= 0);
        else if (op == TOK_GT)
            res = (cmp > 0);
        else if (op == TOK_GTE)
            res = (cmp >= 0);

        as_value_free(&left);
        as_value_free(&right);
        left.type = AS_TYPE_BOOL;
        left.is_null = 0;
        left.u.boolean = res;
    }
    return left;
}

/* "left + right" where at least one side is text: format whatever is not a
 * string and join the two. This lives in its own function so its two line-sized
 * buffers exist only while a join happens, instead of at every level of the
 * recursive descent above. */
static struct as_value as_join_text(struct as_value* left,
                                    struct as_value* right) {
    char b1[AS_LINE_MAX];
    char b2[AS_LINE_MAX];
    const char* s1;
    const char* s2;
    int l1;
    int l2;
    char* joined;
    struct as_value result;

    if (left->type == AS_TYPE_STRING && left->u.str != 0) {
        s1 = left->u.str;
    } else {
        as_value_format(left, b1, sizeof(b1), 0);
        s1 = b1;
    }
    if (right->type == AS_TYPE_STRING && right->u.str != 0) {
        s2 = right->u.str;
    } else {
        as_value_format(right, b2, sizeof(b2), 0);
        s2 = b2;
    }

    l1 = (int)strlen(s1);
    l2 = (int)strlen(s2);
    joined = (char*)kmalloc(l1 + l2 + 1);
    if (joined != 0) {
        memcpy(joined, s1, l1);
        memcpy(joined + l1, s2, l2 + 1);
    }

    as_value_free(left);
    as_value_free(right);

    as_value_init(&result, AS_TYPE_STRING, AS_TYPE_VOID);
    result.is_null = (joined == 0);
    result.u.str = joined;

    return result;
}

/* Addition and subtraction, which also joins strings. */
static struct as_value as_parse_add(struct as_env* env, struct as_lexer* lex) {
    struct as_value left = as_parse_mul(env, lex);
    while (lex->cur.kind == TOK_PLUS || lex->cur.kind == TOK_MINUS) {
        enum as_tok_kind op = lex->cur.kind;
        as_lexer_next(lex);
        struct as_value right = as_parse_mul(env, lex);

        if (op == TOK_PLUS) {
            if (left.type == AS_TYPE_STRING || right.type == AS_TYPE_STRING) {
                left = as_join_text(&left, &right);
            } else {
                // Add in unsigned arithmetic: signed overflow is undefined and
                // an -O2 build is free to do anything with it.
                int32_t sum = (int32_t)((uint32_t)as_value_to_i32(&left) +
                                        (uint32_t)as_value_to_i32(&right));
                as_value_free(&left);
                as_value_free(&right);
                left.type = AS_TYPE_INT32;
                left.is_null = 0;
                left.u.i32 = sum;
            }
        } else {
            int32_t diff = (int32_t)((uint32_t)as_value_to_i32(&left) -
                                     (uint32_t)as_value_to_i32(&right));
            as_value_free(&left);
            as_value_free(&right);
            left.type = AS_TYPE_INT32;
            left.is_null = 0;
            left.u.i32 = diff;
        }
    }
    return left;
}

/* Multiplication, division and remainder. */
static struct as_value as_parse_mul(struct as_env* env, struct as_lexer* lex) {
    struct as_value left = as_parse_unary(env, lex);
    while (lex->cur.kind == TOK_STAR || lex->cur.kind == TOK_SLASH ||
           lex->cur.kind == TOK_PERCENT) {
        enum as_tok_kind op = lex->cur.kind;
        as_lexer_next(lex);
        struct as_value right = as_parse_unary(env, lex);
        int32_t a = as_value_to_i32(&left);
        int32_t b = as_value_to_i32(&right);
        int32_t res = 0;

        if (op == TOK_STAR) {
            res = (int32_t)((uint32_t)a * (uint32_t)b);
        } else if (op == TOK_SLASH) {
            if (b == 0) {
                as_errorf(env, "division by zero.");
            } else if (a == INT32_MIN && b == -1) {
                // The result does not fit and idiv would fault the CPU.
                as_errorf(env, "integer overflow in division.");
            } else {
                res = a / b;
            }
        } else if (op == TOK_PERCENT) {
            if (b == 0) {
                as_errorf(env, "modulo by zero.");
            } else if (a == INT32_MIN && b == -1) {
                // The remainder is 0, but idiv faults before it gets there.
                res = 0;
            } else {
                res = a % b;
            }
        }

        as_value_free(&left);
        as_value_free(&right);
        left.type = AS_TYPE_INT32;
        left.is_null = 0;
        left.u.i32 = res;
    }
    return left;
}

static struct as_value as_parse_unary(struct as_env* env,
                                      struct as_lexer* lex) {
    if (lex->cur.kind == TOK_MINUS) {
        struct as_value val;
        struct as_value res;
        int32_t num;

        as_lexer_next(lex);

        if (!as_nest(env, lex)) {
            return as_error_value();
        }

        val = as_parse_unary(env, lex);
        lex->depth--;

        // Negating INT32_MIN overflows; wrapping is the defined answer.
        num = (int32_t)(0u - (uint32_t)as_value_to_i32(&val));
        as_value_free(&val);

        as_value_init(&res, AS_TYPE_INT32, AS_TYPE_VOID);
        res.u.i32 = num;
        return res;
    }
    if (lex->cur.kind == TOK_NOT) {
        struct as_value val;
        struct as_value res;
        int b;

        as_lexer_next(lex);

        if (!as_nest(env, lex)) {
            return as_error_value();
        }

        val = as_parse_unary(env, lex);
        lex->depth--;

        b = !as_value_is_truthy(&val);
        as_value_free(&val);

        as_value_init(&res, AS_TYPE_BOOL, AS_TYPE_VOID);
        res.u.boolean = b;
        return res;
    }
    return as_parse_primary(env, lex);
}

/* A built-in function call: the name, then exactly the arguments it takes.
 * Each argument binds tighter than any operator, so "lengthof name + 1" adds
 * one to the length instead of measuring the joined text. */
static struct as_value as_parse_function(struct as_env* env,
                                         struct as_lexer* lex,
                                         const char* name) {
    struct as_value args[AS_FUNCTION_MAX_ARGS];
    struct as_value result;
    int arity = as_function_arity(name);
    int got = 0;
    int failed = 0;

    for (; got < arity; got++) {
        if (!as_nest(env, lex)) {
            failed = 1;
            break;
        }
        args[got] = as_parse_unary(env, lex);
        lex->depth--;
        if (env->error) {
            failed = 1;
            break;
        }
    }

    if (failed) {
        for (int i = 0; i < got; i++) {
            as_value_free(&args[i]);
        }
        return as_error_value();
    }

    result = as_function_call(env, name, args, arity);

    for (int i = 0; i < arity; i++) {
        as_value_free(&args[i]);
    }

    return result;
}

static struct as_value as_parse_primary(struct as_env* env,
                                        struct as_lexer* lex) {
    struct as_value val;
    as_value_init(&val, AS_TYPE_INT32, AS_TYPE_VOID);

    if (lex->cur.kind == TOK_INT) {
        val.type = AS_TYPE_INT32;
        val.is_null = 0;
        val.u.i32 = lex->cur.int_val;
        as_lexer_next(lex);
        return val;
    }

    if (lex->cur.kind == TOK_STRING) {
        val.type = AS_TYPE_STRING;
        val.is_null = 0;
        val.u.str = as_strdup(lex->cur.text);
        as_lexer_next(lex);
        return val;
    }

    if (lex->cur.kind == TOK_TRUE) {
        val.type = AS_TYPE_BOOL;
        val.is_null = 0;
        val.u.boolean = 1;
        as_lexer_next(lex);
        return val;
    }

    if (lex->cur.kind == TOK_FALSE) {
        val.type = AS_TYPE_BOOL;
        val.is_null = 0;
        val.u.boolean = 0;
        as_lexer_next(lex);
        return val;
    }

    if (lex->cur.kind == TOK_NULL) {
        val.type = AS_TYPE_STRING;
        val.is_null = 1;
        val.u.str = 0;
        as_lexer_next(lex);
        return val;
    }

    if (lex->cur.kind == TOK_LPAREN) {
        as_lexer_next(lex);

        if (!as_nest(env, lex)) {
            return as_error_value();
        }

        val = as_parse_or(env, lex);
        lex->depth--;

        if (lex->cur.kind == TOK_RPAREN) {
            as_lexer_next(lex);
        }
        return val;
    }

    // Array literal; { 1, 2, 3 }
    if (lex->cur.kind == TOK_LBRACE) {
        as_lexer_next(lex);

        if (!as_nest(env, lex)) {
            return as_error_value();
        }

        val.type = AS_TYPE_ARRAY;
        val.elem_type = AS_TYPE_INT32;
        val.is_null = 0;
        val.u.arr.items = 0;
        val.u.arr.count = 0;
        val.u.arr.capacity = 0;

        while (lex->cur.kind != TOK_RBRACE && lex->cur.kind != TOK_EOF) {
            struct as_value item = as_parse_or(env, lex);
            if (val.u.arr.count == 0) {
                val.elem_type = item.type;
            }

            // A full array must not swallow the rest of the literal without a
            // word: the script would go on with items missing.
            if (!as_array_append(&val, &item)) {
                as_value_free(&item);
                as_value_free(&val);
                as_errorf(env,
                          "an item could not be added to the array (limit "
                          "%d, or out of memory).",
                          AS_ARRAY_MAX);
                return as_error_value();
            }
            as_value_free(&item);
            if (lex->cur.kind == TOK_COMMA) {
                as_lexer_next(lex);
            }
        }
        if (lex->cur.kind == TOK_RBRACE) {
            as_lexer_next(lex);
        }

        lex->depth--;
        return val;
    }

    // Identifier and variable access
    if (lex->cur.kind == TOK_IDENT) {
        char name[AS_NAME_MAX];

        // Cutting a name down here would quietly read a different variable
        // that happens to share its first characters.
        if (strlen(lex->cur.text) >= sizeof(name)) {
            as_errorf(env, "a variable name is longer than %d characters.",
                      AS_NAME_MAX - 1);
            return as_error_value();
        }

        strncpy(name, lex->cur.text, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
        as_lexer_next(lex);

        struct as_var* var = as_env_lookup(env, name);
        if (var == 0) {
            if (as_function_arity(name) > 0) {
                return as_parse_function(env, lex, name);
            }

            // A command that prints text acts as a string value here, so
            // "pwd + \"/programs\"" reads the directory the command shows.
            if (command_returns_output(name)) {
                as_run_command_value(env, name, &val);
            } else {
                as_errorf(env, "variable '%s' not found.", name);
            }
            return val;
        }

        // Array indexing: arr[index]
        if (lex->cur.kind == TOK_LBRACKET) {
            as_lexer_next(lex);
            struct as_value idx_val = as_parse_or(env, lex);
            if (lex->cur.kind == TOK_RBRACKET) {
                as_lexer_next(lex);
            }
            int idx = as_value_to_i32(&idx_val);
            as_value_free(&idx_val);

            if (var->val.type != AS_TYPE_ARRAY) {
                as_errorf(env, "'%s' is not an array.", name);
                return val;
            }
            if (idx < 0 || idx >= var->val.u.arr.count) {
                as_errorf(env, "array index %d out of bounds for '%s'.", idx,
                          name);
                return val;
            }
            as_value_copy(&val, &var->val.u.arr.items[idx]);
            return val;
        }

        as_value_copy(&val, &var->val);
        return val;
    }

    as_lexer_next(lex);
    return val;
}

/* Evaluate an expression string. */
struct as_value as_eval_expression(struct as_env* env, const char* str) {
    struct as_lexer lex;
    lex.src = str;
    lex.pos = 0;
    lex.depth = 0;
    as_lexer_next(&lex);
    return as_parse_or(env, &lex);
}
