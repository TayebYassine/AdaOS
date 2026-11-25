#include "as_parser.h"

#include "as_command.h"
#include "as_env.h"
#include "as_error.h"
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

/* Addition and subtraction, which also joins strings. */
static struct as_value as_parse_add(struct as_env* env, struct as_lexer* lex) {
    struct as_value left = as_parse_mul(env, lex);
    while (lex->cur.kind == TOK_PLUS || lex->cur.kind == TOK_MINUS) {
        enum as_tok_kind op = lex->cur.kind;
        as_lexer_next(lex);
        struct as_value right = as_parse_mul(env, lex);

        if (op == TOK_PLUS) {
            if (left.type == AS_TYPE_STRING || right.type == AS_TYPE_STRING) {
                char b1[AS_LINE_MAX];
                char b2[AS_LINE_MAX];
                const char* s1;
                const char* s2;

                if (left.type == AS_TYPE_STRING && left.u.str != 0) {
                    s1 = left.u.str;
                } else {
                    as_value_format(&left, b1, sizeof(b1), 0);
                    s1 = b1;
                }
                if (right.type == AS_TYPE_STRING && right.u.str != 0) {
                    s2 = right.u.str;
                } else {
                    as_value_format(&right, b2, sizeof(b2), 0);
                    s2 = b2;
                }

                int l1 = (int)strlen(s1);
                int l2 = (int)strlen(s2);
                char* joined = (char*)kmalloc(l1 + l2 + 1);
                if (joined != 0) {
                    memcpy(joined, s1, l1);
                    memcpy(joined + l1, s2, l2 + 1);
                }
                as_value_free(&left);
                as_value_free(&right);
                left.type = AS_TYPE_STRING;
                left.is_null = (joined == 0);
                left.u.str = joined;
            } else {
                int32_t sum = as_value_to_i32(&left) + as_value_to_i32(&right);
                as_value_free(&left);
                as_value_free(&right);
                left.type = AS_TYPE_INT32;
                left.is_null = 0;
                left.u.i32 = sum;
            }
        } else {
            int32_t diff = as_value_to_i32(&left) - as_value_to_i32(&right);
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
            res = a * b;
        } else if (op == TOK_SLASH) {
            if (b == 0) {
                as_errorf(env, "division by zero.");
            } else {
                res = a / b;
            }
        } else if (op == TOK_PERCENT) {
            if (b == 0) {
                as_errorf(env, "modulo by zero.");
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
        as_lexer_next(lex);
        struct as_value val = as_parse_unary(env, lex);
        int32_t num = -as_value_to_i32(&val);
        as_value_free(&val);
        struct as_value res;
        as_value_init(&res, AS_TYPE_INT32, AS_TYPE_VOID);
        res.u.i32 = num;
        return res;
    }
    if (lex->cur.kind == TOK_NOT) {
        as_lexer_next(lex);
        struct as_value val = as_parse_unary(env, lex);
        int b = !as_value_is_truthy(&val);
        as_value_free(&val);
        struct as_value res;
        as_value_init(&res, AS_TYPE_BOOL, AS_TYPE_VOID);
        res.u.boolean = b;
        return res;
    }
    return as_parse_primary(env, lex);
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
        val = as_parse_or(env, lex);
        if (lex->cur.kind == TOK_RPAREN) {
            as_lexer_next(lex);
        }
        return val;
    }

    if (lex->cur.kind == TOK_LEN) {
        as_lexer_next(lex);
        if (lex->cur.kind == TOK_LPAREN) {
            as_lexer_next(lex);
        }
        struct as_value target = as_parse_or(env, lex);
        if (lex->cur.kind == TOK_RPAREN) {
            as_lexer_next(lex);
        }
        int len_val = 0;
        if (target.type == AS_TYPE_STRING && target.u.str != 0) {
            len_val = (int)strlen(target.u.str);
        } else if (target.type == AS_TYPE_ARRAY) {
            len_val = target.u.arr.count;
        }
        as_value_free(&target);
        val.type = AS_TYPE_INT32;
        val.is_null = 0;
        val.u.i32 = len_val;
        return val;
    }

    // Array literal; { 1, 2, 3 }
    if (lex->cur.kind == TOK_LBRACE) {
        as_lexer_next(lex);
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
            as_array_append(&val, &item);
            as_value_free(&item);
            if (lex->cur.kind == TOK_COMMA) {
                as_lexer_next(lex);
            }
        }
        if (lex->cur.kind == TOK_RBRACE) {
            as_lexer_next(lex);
        }
        return val;
    }

    // Identifier and variable access
    if (lex->cur.kind == TOK_IDENT) {
        char name[AS_NAME_MAX];
        strncpy(name, lex->cur.text, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
        as_lexer_next(lex);

        struct as_var* var = as_env_lookup(env, name);
        if (var == 0) {
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
    as_lexer_next(&lex);
    return as_parse_or(env, &lex);
}
