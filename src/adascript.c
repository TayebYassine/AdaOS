#include "adascript.h"

#include <stddef.h>
#include <stdint.h>

#include "command.h"
#include "fs.h"
#include "kmalloc.h"
#include "kprintf.h"
#include "libc.h"
#include "screen.h"
#include "shell.h"
#include "timer.h"

#define AS_NAME_MAX 32
#define AS_LINE_MAX 256
#define AS_ARRAY_MAX 256

/* AdaScript data types. */
enum as_type {
    AS_TYPE_VOID,
    AS_TYPE_INT8,
    AS_TYPE_UINT8,
    AS_TYPE_INT16,
    AS_TYPE_UINT16,
    AS_TYPE_INT32,
    AS_TYPE_UINT32,
    AS_TYPE_STRING,
    AS_TYPE_BOOL,
    AS_TYPE_ARRAY,
};

/* Value container supporting primitives, strings, and arrays. */
struct as_value {
    enum as_type type;
    /* Base type when type is AS_TYPE_ARRAY */
    enum as_type elem_type;
    int is_null;
    union {
        int32_t i32;
        uint32_t u32;
        char* str;
        int boolean;
        struct {
            struct as_value* items;
            int count;
            int capacity;
        } arr;
    } u;
};

/* One variable in RAM. */
struct as_var {
    char name[AS_NAME_MAX];
    struct as_value val;
    struct as_var* next;
};

/* Execution environment and symbol table. */
struct as_env {
    struct as_var* vars;
    int error;
    int exit_flag;
    int break_flag;
    int continue_flag;
};

/* One parsed line in the script. */
struct as_line {
    char* text;
    int orig_line;
    int indent;
};

/* Forward declarations. */
static void as_value_free(struct as_value* val);
static void as_value_copy(struct as_value* dst, const struct as_value* src);
static void as_value_format(const struct as_value* val, char* buf, int max,
                            int quote_strings);
static int as_value_to_i32(const struct as_value* val);
static uint32_t as_value_to_u32(const struct as_value* val);
static int as_value_is_truthy(const struct as_value* val);
static void as_value_cast_assign(struct as_value* dst,
                                 const struct as_value* src);
static struct as_value as_eval_expression(struct as_env* env, const char* str);
static int as_exec_block(struct as_env* env, struct as_line* lines, int start,
                         int end);

/* Duplicate a string using kernel heap memory. */
static char* as_strdup(const char* s) {
    if (s == 0) {
        return 0;
    }
    size_t len = strlen(s);
    char* copy = (char*)kmalloc(len + 1);
    if (copy != 0) {
        memcpy(copy, s, len + 1);
    }
    return copy;
}

/* Initialize a value with defaults. */
static void as_value_init(struct as_value* val, enum as_type type,
                          enum as_type elem_type) {
    val->type = type;
    val->elem_type = elem_type;
    val->is_null = 0;

    switch (type) {
        case AS_TYPE_INT8:
        case AS_TYPE_INT16:
        case AS_TYPE_INT32:
            val->u.i32 = 0;
            break;
        case AS_TYPE_UINT8:
        case AS_TYPE_UINT16:
        case AS_TYPE_UINT32:
            val->u.u32 = 0;
            break;
        case AS_TYPE_BOOL:
            val->u.boolean = 0;
            break;
        case AS_TYPE_STRING:
            val->is_null = 1;
            val->u.str = 0;
            break;
        case AS_TYPE_ARRAY:
            val->u.arr.items = 0;
            val->u.arr.count = 0;
            val->u.arr.capacity = 0;
            break;
        default:
            val->u.i32 = 0;
            break;
    }
}

/* Free any dynamic memory associated with a value. */
static void as_value_free(struct as_value* val) {
    if (val->type == AS_TYPE_STRING) {
        if (val->u.str != 0) {
            kfree(val->u.str);
            val->u.str = 0;
        }
    } else if (val->type == AS_TYPE_ARRAY) {
        if (val->u.arr.items != 0) {
            for (int i = 0; i < val->u.arr.count; i++) {
                as_value_free(&val->u.arr.items[i]);
            }
            kfree(val->u.arr.items);
            val->u.arr.items = 0;
        }
        val->u.arr.count = 0;
        val->u.arr.capacity = 0;
    }
}

/* Deep-copy a value from src to dst. */
static void as_value_copy(struct as_value* dst, const struct as_value* src) {
    dst->type = src->type;
    dst->elem_type = src->elem_type;
    dst->is_null = src->is_null;

    if (src->type == AS_TYPE_STRING) {
        dst->u.str = src->u.str ? as_strdup(src->u.str) : 0;
    } else if (src->type == AS_TYPE_ARRAY) {
        dst->u.arr.count = src->u.arr.count;
        dst->u.arr.capacity = src->u.arr.count;
        if (src->u.arr.count > 0) {
            dst->u.arr.items = (struct as_value*)kmalloc(
                src->u.arr.count * sizeof(struct as_value));
            if (dst->u.arr.items != 0) {
                for (int i = 0; i < src->u.arr.count; i++) {
                    as_value_copy(&dst->u.arr.items[i], &src->u.arr.items[i]);
                }
            }
        } else {
            dst->u.arr.items = 0;
        }
    } else {
        dst->u = src->u;
    }
}

/* Append an item to an array. */
static int as_array_append(struct as_value* arr, const struct as_value* item) {
    if (arr->type != AS_TYPE_ARRAY) {
        return 0;
    }

    if (arr->u.arr.count >= arr->u.arr.capacity) {
        int new_cap = (arr->u.arr.capacity == 0) ? 4 : arr->u.arr.capacity * 2;
        if (new_cap > AS_ARRAY_MAX) {
            new_cap = AS_ARRAY_MAX;
        }
        if (arr->u.arr.count >= new_cap) {
            return 0;
        }

        struct as_value* new_items =
            (struct as_value*)kmalloc(new_cap * sizeof(struct as_value));
        if (new_items == 0) {
            return 0;
        }

        for (int i = 0; i < arr->u.arr.count; i++) {
            new_items[i] = arr->u.arr.items[i];
        }

        if (arr->u.arr.items != 0) {
            kfree(arr->u.arr.items);
        }

        arr->u.arr.items = new_items;
        arr->u.arr.capacity = new_cap;
    }

    if (arr->elem_type == AS_TYPE_VOID ||
        (arr->elem_type == AS_TYPE_INT32 && arr->u.arr.count == 0)) {
        arr->elem_type = item->type;
    }

    as_value_init(&arr->u.arr.items[arr->u.arr.count], arr->elem_type,
                  AS_TYPE_VOID);
    as_value_cast_assign(&arr->u.arr.items[arr->u.arr.count], item);
    arr->u.arr.count++;
    return 1;
}

/* Convert value to signed 32-bit integer. */
static int32_t as_value_to_i32(const struct as_value* val) {
    if (val->is_null) {
        return 0;
    }

    switch (val->type) {
        case AS_TYPE_INT8:
        case AS_TYPE_INT16:
        case AS_TYPE_INT32:
            return val->u.i32;
        case AS_TYPE_UINT8:
        case AS_TYPE_UINT16:
        case AS_TYPE_UINT32:
            return (int32_t)val->u.u32;
        case AS_TYPE_BOOL:
            return val->u.boolean ? 1 : 0;
        case AS_TYPE_STRING:
            return val->u.str ? atoi(val->u.str) : 0;
        default:
            return 0;
    }
}

/* Convert value to unsigned 32-bit integer. */
static uint32_t as_value_to_u32(const struct as_value* val) {
    if (val->is_null) {
        return 0;
    }

    switch (val->type) {
        case AS_TYPE_UINT8:
        case AS_TYPE_UINT16:
        case AS_TYPE_UINT32:
            return val->u.u32;
        case AS_TYPE_INT8:
        case AS_TYPE_INT16:
        case AS_TYPE_INT32:
            return (uint32_t)val->u.i32;
        case AS_TYPE_BOOL:
            return val->u.boolean ? 1 : 0;
        case AS_TYPE_STRING:
            return val->u.str ? (uint32_t)atoi(val->u.str) : 0;
        default:
            return 0;
    }
}

/* Check if a value evaluates to boolean true. */
static int as_value_is_truthy(const struct as_value* val) {
    if (val->is_null) {
        return 0;
    }

    switch (val->type) {
        case AS_TYPE_BOOL:
            return val->u.boolean != 0;
        case AS_TYPE_INT8:
        case AS_TYPE_INT16:
        case AS_TYPE_INT32:
            return val->u.i32 != 0;
        case AS_TYPE_UINT8:
        case AS_TYPE_UINT16:
        case AS_TYPE_UINT32:
            return val->u.u32 != 0;
        case AS_TYPE_STRING:
            return val->u.str != 0 && val->u.str[0] != '\0';
        case AS_TYPE_ARRAY:
            return val->u.arr.count > 0;
        default:
            return 0;
    }
}

/* Format a value into a human-readable text string. */
static void as_value_format(const struct as_value* val, char* buf, int max,
                            int quote_strings) {
    if (val->is_null) {
        strncpy(buf, "null", max);
        return;
    }

    switch (val->type) {
        case AS_TYPE_INT8:
        case AS_TYPE_INT16:
        case AS_TYPE_INT32:
            itoa(val->u.i32, buf);
            break;
        case AS_TYPE_UINT8:
        case AS_TYPE_UINT16:
        case AS_TYPE_UINT32:
            utoa(val->u.u32, buf);
            break;
        case AS_TYPE_BOOL:
            strncpy(buf, val->u.boolean ? "true" : "false", max);
            break;
        case AS_TYPE_STRING: {
            if (val->u.str == 0) {
                strncpy(buf, "null", max);
            } else if (quote_strings) {
                int len = (int)strlen(val->u.str);
                if (len + 3 < max) {
                    buf[0] = '"';
                    memcpy(buf + 1, val->u.str, len);
                    buf[len + 1] = '"';
                    buf[len + 2] = '\0';
                } else {
                    strncpy(buf, val->u.str, max);
                }
            } else {
                strncpy(buf, val->u.str, max);
            }
            break;
        }
        case AS_TYPE_ARRAY: {
            int pos = 0;
            if (pos < max - 1) {
                buf[pos++] = '[';
            }
            if (pos < max - 1) {
                buf[pos++] = ' ';
            }
            for (int i = 0; i < val->u.arr.count; i++) {
                if (i > 0) {
                    if (pos < max - 2) {
                        buf[pos++] = ',';
                        buf[pos++] = ' ';
                    }
                }
                char item_buf[64];
                as_value_format(&val->u.arr.items[i], item_buf,
                                sizeof(item_buf), 1);
                int item_len = (int)strlen(item_buf);
                if (pos + item_len < max - 2) {
                    memcpy(buf + pos, item_buf, item_len);
                    pos += item_len;
                }
            }
            if (pos < max - 2) {
                buf[pos++] = ' ';
                buf[pos++] = ']';
            }
            buf[pos] = '\0';
            break;
        }
        default:
            buf[0] = '\0';
            break;
    }
}

/* Cast and assign src value into dst variable according to dst's type. */
static void as_value_cast_assign(struct as_value* dst,
                                 const struct as_value* src) {
    if (src->is_null) {
        as_value_free(dst);
        dst->is_null = 1;
        if (dst->type == AS_TYPE_STRING) {
            dst->u.str = 0;
        }
        return;
    }

    dst->is_null = 0;

    switch (dst->type) {
        case AS_TYPE_INT8:
            dst->u.i32 = (int8_t)as_value_to_i32(src);
            break;
        case AS_TYPE_UINT8:
            dst->u.u32 = (uint8_t)as_value_to_u32(src);
            break;
        case AS_TYPE_INT16:
            dst->u.i32 = (int16_t)as_value_to_i32(src);
            break;
        case AS_TYPE_UINT16:
            dst->u.u32 = (uint16_t)as_value_to_u32(src);
            break;
        case AS_TYPE_INT32:
            dst->u.i32 = as_value_to_i32(src);
            break;
        case AS_TYPE_UINT32:
            dst->u.u32 = as_value_to_u32(src);
            break;
        case AS_TYPE_BOOL:
            dst->u.boolean = as_value_is_truthy(src);
            break;
        case AS_TYPE_STRING: {
            as_value_free(dst);
            char buf[AS_LINE_MAX];
            as_value_format(src, buf, sizeof(buf), 0);
            dst->u.str = as_strdup(buf);
            break;
        }
        case AS_TYPE_ARRAY: {
            if (src->type == AS_TYPE_ARRAY) {
                enum as_type target_elem = (dst->elem_type != AS_TYPE_VOID)
                                               ? dst->elem_type
                                               : src->elem_type;
                as_value_free(dst);
                dst->type = AS_TYPE_ARRAY;
                dst->elem_type = target_elem;
                dst->is_null = 0;
                dst->u.arr.count = src->u.arr.count;
                dst->u.arr.capacity = src->u.arr.count;
                if (src->u.arr.count > 0) {
                    dst->u.arr.items = (struct as_value*)kmalloc(
                        src->u.arr.count * sizeof(struct as_value));
                    if (dst->u.arr.items != 0) {
                        for (int i = 0; i < src->u.arr.count; i++) {
                            as_value_init(&dst->u.arr.items[i], target_elem,
                                          AS_TYPE_VOID);
                            as_value_cast_assign(&dst->u.arr.items[i],
                                                 &src->u.arr.items[i]);
                        }
                    }
                } else {
                    dst->u.arr.items = 0;
                }
            }
            break;
        }
        default:
            break;
    }
}

/* Find a variable by name in the environment. */
static struct as_var* as_env_lookup(struct as_env* env, const char* name) {
    for (struct as_var* v = env->vars; v != 0; v = v->next) {
        if (strcmp(v->name, name) == 0) {
            return v;
        }
    }
    return 0;
}

/* Register a new variable in RAM. */
static struct as_var* as_env_define(struct as_env* env, const char* name,
                                    enum as_type type, enum as_type elem_type) {
    if (as_env_lookup(env, name) != 0) {
        return 0;
    }

    struct as_var* var = (struct as_var*)kmalloc(sizeof(struct as_var));
    if (var == 0) {
        return 0;
    }

    strncpy(var->name, name, AS_NAME_MAX - 1);
    var->name[AS_NAME_MAX - 1] = '\0';
    as_value_init(&var->val, type, elem_type);

    var->next = env->vars;
    env->vars = var;
    return var;
}

/* Unregister a variable from RAM and free its memory. */
static int as_env_delete(struct as_env* env, const char* name) {
    struct as_var* prev = 0;
    for (struct as_var* v = env->vars; v != 0; prev = v, v = v->next) {
        if (strcmp(v->name, name) == 0) {
            if (prev != 0) {
                prev->next = v->next;
            } else {
                env->vars = v->next;
            }
            as_value_free(&v->val);
            kfree(v);
            return 1;
        }
    }
    return 0;
}

/* Free all variables remaining in RAM. */
static void as_env_cleanup(struct as_env* env) {
    struct as_var* v = env->vars;
    while (v != 0) {
        struct as_var* next = v->next;
        as_value_free(&v->val);
        kfree(v);
        v = next;
    }
    env->vars = 0;
}

/* Parse type name string like "int8", "string", "uint32[]". */
static int as_parse_type(const char* s, enum as_type* out_type,
                         enum as_type* out_elem) {
    int len = (int)strlen(s);
    int is_arr = 0;
    char base[32];

    if (len > 2 && s[len - 2] == '[' && s[len - 1] == ']') {
        is_arr = 1;
        if (len - 2 >= (int)sizeof(base)) {
            return 0;
        }
        memcpy(base, s, len - 2);
        base[len - 2] = '\0';
    } else {
        if (len >= (int)sizeof(base)) {
            return 0;
        }
        strcpy(base, s);
    }

    enum as_type t;
    if (strcmp(base, "int8") == 0) {
        t = AS_TYPE_INT8;
    } else if (strcmp(base, "uint8") == 0) {
        t = AS_TYPE_UINT8;
    } else if (strcmp(base, "int16") == 0) {
        t = AS_TYPE_INT16;
    } else if (strcmp(base, "uint16") == 0) {
        t = AS_TYPE_UINT16;
    } else if (strcmp(base, "int32") == 0 || strcmp(base, "int") == 0) {
        t = AS_TYPE_INT32;
    } else if (strcmp(base, "uint32") == 0 || strcmp(base, "uint") == 0) {
        t = AS_TYPE_UINT32;
    } else if (strcmp(base, "string") == 0) {
        t = AS_TYPE_STRING;
    } else if (strcmp(base, "bool") == 0) {
        t = AS_TYPE_BOOL;
    } else {
        return 0;
    }

    if (is_arr) {
        *out_type = AS_TYPE_ARRAY;
        *out_elem = t;
    } else {
        *out_type = t;
        *out_elem = AS_TYPE_VOID;
    }

    return 1;
}

/* Tokenizer for expressions. */
enum as_tok_kind {
    TOK_EOF = 0,
    TOK_INT,
    TOK_STRING,
    TOK_IDENT,
    TOK_TRUE,
    TOK_FALSE,
    TOK_NULL,
    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_PERCENT,
    TOK_EQ,
    TOK_NEQ,
    TOK_LT,
    TOK_LTE,
    TOK_GT,
    TOK_GTE,
    TOK_AND,
    TOK_OR,
    TOK_NOT,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACKET,
    TOK_RBRACKET,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_COMMA,
    TOK_LEN,
};

struct as_token {
    enum as_tok_kind kind;
    int32_t int_val;
    char text[64];
};

struct as_lexer {
    const char* src;
    int pos;
    struct as_token cur;
};

/* Advance lexer to next token. */
static void as_lexer_next(struct as_lexer* lex) {
    while (lex->src[lex->pos] == ' ' || lex->src[lex->pos] == '\t' ||
           lex->src[lex->pos] == '\r' || lex->src[lex->pos] == '\n') {
        lex->pos++;
    }

    char c = lex->src[lex->pos];
    if (c == '\0') {
        lex->cur.kind = TOK_EOF;
        lex->cur.text[0] = '\0';
        return;
    }

    // Integer literal; decimal or hex
    if ((c >= '0' && c <= '9') ||
        (c == '0' &&
         (lex->src[lex->pos + 1] == 'x' || lex->src[lex->pos + 1] == 'X'))) {
        int hex = 0;
        int32_t val = 0;

        if (c == '0' &&
            (lex->src[lex->pos + 1] == 'x' || lex->src[lex->pos + 1] == 'X')) {
            hex = 1;
            lex->pos += 2;
        }

        while (1) {
            char ch = lex->src[lex->pos];
            if (ch >= '0' && ch <= '9') {
                val = hex ? (val * 16 + (ch - '0')) : (val * 10 + (ch - '0'));
                lex->pos++;
            } else if (hex && ch >= 'a' && ch <= 'f') {
                val = val * 16 + (ch - 'a' + 10);
                lex->pos++;
            } else if (hex && ch >= 'A' && ch <= 'F') {
                val = val * 16 + (ch - 'A' + 10);
                lex->pos++;
            } else {
                break;
            }
        }

        lex->cur.kind = TOK_INT;
        lex->cur.int_val = val;
        itoa(val, lex->cur.text);
        return;
    }

    // String literal
    if (c == '"') {
        lex->pos++;
        int idx = 0;
        while (lex->src[lex->pos] != '\0' && lex->src[lex->pos] != '"') {
            char ch = lex->src[lex->pos++];
            if (ch == '\\') {
                char esc = lex->src[lex->pos++];
                if (esc == 'n')
                    ch = '\n';
                else if (esc == 't')
                    ch = '\t';
                else if (esc == '"')
                    ch = '"';
                else if (esc == '\\')
                    ch = '\\';
                else
                    ch = esc;
            }
            if (idx < (int)sizeof(lex->cur.text) - 1) {
                lex->cur.text[idx++] = ch;
            }
        }
        if (lex->src[lex->pos] == '"') {
            lex->pos++;
        }
        lex->cur.text[idx] = '\0';
        lex->cur.kind = TOK_STRING;
        return;
    }

    // Identifier or keyword
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
        int idx = 0;
        while ((lex->src[lex->pos] >= 'a' && lex->src[lex->pos] <= 'z') ||
               (lex->src[lex->pos] >= 'A' && lex->src[lex->pos] <= 'Z') ||
               (lex->src[lex->pos] >= '0' && lex->src[lex->pos] <= '9') ||
               lex->src[lex->pos] == '_') {
            if (idx < (int)sizeof(lex->cur.text) - 1) {
                lex->cur.text[idx++] = lex->src[lex->pos];
            }
            lex->pos++;
        }
        lex->cur.text[idx] = '\0';

        if (strcmp(lex->cur.text, "true") == 0) {
            lex->cur.kind = TOK_TRUE;
        } else if (strcmp(lex->cur.text, "false") == 0) {
            lex->cur.kind = TOK_FALSE;
        } else if (strcmp(lex->cur.text, "null") == 0) {
            lex->cur.kind = TOK_NULL;
        } else if (strcmp(lex->cur.text, "and") == 0) {
            lex->cur.kind = TOK_AND;
        } else if (strcmp(lex->cur.text, "or") == 0) {
            lex->cur.kind = TOK_OR;
        } else if (strcmp(lex->cur.text, "not") == 0) {
            lex->cur.kind = TOK_NOT;
        } else if (strcmp(lex->cur.text, "len") == 0) {
            lex->cur.kind = TOK_LEN;
        } else {
            lex->cur.kind = TOK_IDENT;
        }
        return;
    }

    // Multi-character operators
    if (c == '=' && lex->src[lex->pos + 1] == '=') {
        lex->pos += 2;
        lex->cur.kind = TOK_EQ;
        return;
    }
    if (c == '!' && lex->src[lex->pos + 1] == '=') {
        lex->pos += 2;
        lex->cur.kind = TOK_NEQ;
        return;
    }
    if (c == '<' && lex->src[lex->pos + 1] == '=') {
        lex->pos += 2;
        lex->cur.kind = TOK_LTE;
        return;
    }
    if (c == '>' && lex->src[lex->pos + 1] == '=') {
        lex->pos += 2;
        lex->cur.kind = TOK_GTE;
        return;
    }
    if (c == '&' && lex->src[lex->pos + 1] == '&') {
        lex->pos += 2;
        lex->cur.kind = TOK_AND;
        return;
    }
    if (c == '|' && lex->src[lex->pos + 1] == '|') {
        lex->pos += 2;
        lex->cur.kind = TOK_OR;
        return;
    }

    // Single-character operators and symbols
    lex->pos++;
    switch (c) {
        case '+':
            lex->cur.kind = TOK_PLUS;
            break;
        case '-':
            lex->cur.kind = TOK_MINUS;
            break;
        case '*':
            lex->cur.kind = TOK_STAR;
            break;
        case '/':
            lex->cur.kind = TOK_SLASH;
            break;
        case '%':
            lex->cur.kind = TOK_PERCENT;
            break;
        case '<':
            lex->cur.kind = TOK_LT;
            break;
        case '>':
            lex->cur.kind = TOK_GT;
            break;
        case '!':
            lex->cur.kind = TOK_NOT;
            break;
        case '(':
            lex->cur.kind = TOK_LPAREN;
            break;
        case ')':
            lex->cur.kind = TOK_RPAREN;
            break;
        case '[':
            lex->cur.kind = TOK_LBRACKET;
            break;
        case ']':
            lex->cur.kind = TOK_RBRACKET;
            break;
        case '{':
            lex->cur.kind = TOK_LBRACE;
            break;
        case '}':
            lex->cur.kind = TOK_RBRACE;
            break;
        case ',':
            lex->cur.kind = TOK_COMMA;
            break;
        default:
            lex->cur.kind = TOK_EOF;
            break;
    }
}

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
                as_value_format(&left, b1, sizeof(b1), 0);
                as_value_format(&right, b2, sizeof(b2), 0);
                int l1 = (int)strlen(b1);
                int l2 = (int)strlen(b2);
                char* joined = (char*)kmalloc(l1 + l2 + 1);
                if (joined != 0) {
                    memcpy(joined, b1, l1);
                    memcpy(joined + l1, b2, l2 + 1);
                }
                as_value_free(&left);
                as_value_free(&right);
                left.type = AS_TYPE_STRING;
                left.is_null = 0;
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
                print("Error: division by zero.\n");
                env->error = 1;
            } else {
                res = a / b;
            }
        } else if (op == TOK_PERCENT) {
            if (b == 0) {
                print("Error: modulo by zero.\n");
                env->error = 1;
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
            kprintf("Error: variable '%s' not found.\n", name);
            env->error = 1;
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
                kprintf("Error: '%s' is not an array.\n", name);
                env->error = 1;
                return val;
            }
            if (idx < 0 || idx >= var->val.u.arr.count) {
                kprintf("Error: array index %d out of bounds for '%s'.\n", idx,
                        name);
                env->error = 1;
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
static struct as_value as_eval_expression(struct as_env* env, const char* str) {
    struct as_lexer lex;
    lex.src = str;
    lex.pos = 0;
    as_lexer_next(&lex);
    return as_parse_or(env, &lex);
}

/* Find the matching "done" (or "endif"/"fi") for a block. */
static int as_find_matching_done(struct as_line* lines, int total, int start) {
    int depth = 1;
    for (int i = start + 1; i < total; i++) {
        const char* t = lines[i].text;
        if (strncmp(t, "for ", 4) == 0 || strncmp(t, "while ", 6) == 0 ||
            strncmp(t, "if ", 3) == 0) {
            depth++;
        } else if (strcmp(t, "done") == 0 || strcmp(t, "done;") == 0 ||
                   strcmp(t, "endif") == 0 || strcmp(t, "endif;") == 0 ||
                   strcmp(t, "fi") == 0 || strcmp(t, "fi;") == 0) {
            depth--;
            if (depth == 0) {
                return i;
            }
        }
    }
    return -1;
}

/* Check if a command is disallowed in AdaScript. */
static int as_is_disallowed_command(const char* cmd) {
    return (strcmp(cmd, "snake") == 0 || strcmp(cmd, "tetris") == 0 ||
            strcmp(cmd, "write") == 0 || strcmp(cmd, "reboot") == 0 ||
            strcmp(cmd, "shutdown") == 0 || strcmp(cmd, "settime") == 0 ||
            strcmp(cmd, "setdate") == 0);
}

/* Execute a built-in OS command from AdaScript with variable interpolation. */
static void as_exec_system_cmd(struct as_env* env, const char* line) {
    char expanded[AS_LINE_MAX];
    int exp_pos = 0;
    int i = 0;

    while (line[i] == ' ') i++;
    char cmd[32];
    int cmd_len = 0;
    while (line[i] != '\0' && line[i] != ' ' && line[i] != ';') {
        if (cmd_len < (int)sizeof(cmd) - 1) {
            cmd[cmd_len++] = line[i];
        }
        i++;
    }
    cmd[cmd_len] = '\0';

    if (as_is_disallowed_command(cmd)) {
        kprintf("Error: command '%s' cannot be run in AdaScript.\n", cmd);
        env->error = 1;
        return;
    }

    memcpy(expanded, cmd, cmd_len);
    exp_pos = cmd_len;

    while (line[i] != '\0' && line[i] != ';') {
        if (line[i] == ' ') {
            if (exp_pos < (int)sizeof(expanded) - 1) {
                expanded[exp_pos++] = ' ';
            }
            i++;
            continue;
        }

        if (line[i] == '"') {
            i++;
            while (line[i] != '\0' && line[i] != '"') {
                if (exp_pos < (int)sizeof(expanded) - 1) {
                    expanded[exp_pos++] = line[i];
                }
                i++;
            }
            if (line[i] == '"') i++;
            continue;
        }

        char word[64];
        int w_len = 0;
        int has_dollar = (line[i] == '$');
        if (has_dollar) i++;

        while (line[i] != '\0' && line[i] != ' ' && line[i] != ';') {
            if (w_len < (int)sizeof(word) - 1) {
                word[w_len++] = line[i];
            }
            i++;
        }
        word[w_len] = '\0';

        struct as_var* var = as_env_lookup(env, word);
        if (var != 0) {
            char val_buf[64];
            as_value_format(&var->val, val_buf, sizeof(val_buf), 0);
            int v_len = (int)strlen(val_buf);
            if (exp_pos + v_len < (int)sizeof(expanded) - 1) {
                memcpy(expanded + exp_pos, val_buf, v_len);
                exp_pos += v_len;
            }
        } else {
            if (has_dollar && exp_pos < (int)sizeof(expanded) - 1) {
                expanded[exp_pos++] = '$';
            }
            if (exp_pos + w_len < (int)sizeof(expanded) - 1) {
                memcpy(expanded + exp_pos, word, w_len);
                exp_pos += w_len;
            }
        }
    }

    expanded[exp_pos] = '\0';
    command_execute(expanded);
}

/* Execute a "define" statement. */
static void as_exec_define(struct as_env* env, const char* stmt) {
    const char* p = stmt + 6; /* skip "define" */
    while (*p == ' ') p++;

    char name[AS_NAME_MAX];
    int n_len = 0;
    while (*p != '\0' && *p != ':' && *p != ' ' && *p != '=') {
        if (n_len < (int)sizeof(name) - 1) {
            name[n_len++] = *p;
        }
        p++;
    }
    name[n_len] = '\0';

    while (*p == ' ') p++;
    if (*p == ':') p++;
    while (*p == ' ') p++;

    char type_str[32];
    int t_len = 0;
    while (*p != '\0' && *p != '=' && *p != ' ' && *p != ';') {
        if (t_len < (int)sizeof(type_str) - 1) {
            type_str[t_len++] = *p;
        }
        p++;
    }
    type_str[t_len] = '\0';

    enum as_type type;
    enum as_type elem_type;
    if (!as_parse_type(type_str, &type, &elem_type)) {
        kprintf("Error: unknown type '%s'.\n", type_str);
        env->error = 1;
        return;
    }

    struct as_var* var = as_env_define(env, name, type, elem_type);
    if (var == 0) {
        kprintf("Error: variable '%s' already defined.\n", name);
        env->error = 1;
        return;
    }

    while (*p == ' ') p++;
    if (*p == '=') {
        p++;
        while (*p == ' ') p++;

        char expr_buf[AS_LINE_MAX];
        strncpy(expr_buf, p, sizeof(expr_buf) - 1);
        expr_buf[sizeof(expr_buf) - 1] = '\0';
        int e_len = (int)strlen(expr_buf);
        while (e_len > 0 &&
               (expr_buf[e_len - 1] == ' ' || expr_buf[e_len - 1] == ';')) {
            expr_buf[--e_len] = '\0';
        }

        struct as_value val = as_eval_expression(env, expr_buf);
        as_value_cast_assign(&var->val, &val);
        as_value_free(&val);
    }
}

/* Execute a "delete" statement. */
static void as_exec_delete(struct as_env* env, const char* stmt) {
    const char* p = stmt + 6;
    while (*p == ' ') p++;

    char name[AS_NAME_MAX];
    int n_len = 0;
    while (*p != '\0' && *p != ' ' && *p != ';') {
        if (n_len < (int)sizeof(name) - 1) {
            name[n_len++] = *p;
        }
        p++;
    }
    name[n_len] = '\0';

    if (!as_env_delete(env, name)) {
        kprintf("Error: variable '%s' not found.\n", name);
        env->error = 1;
    }
}

/* Execute a "print" statement. */
static void as_exec_print(struct as_env* env, const char* stmt) {
    const char* p = stmt + 5;
    while (*p == ' ') p++;

    if (*p == '\0' || *p == ';') {
        print_char('\n');
        return;
    }

    int first = 1;
    while (*p != '\0' && *p != ';') {
        char arg_buf[AS_LINE_MAX];
        int a_len = 0;
        int in_quote = 0;
        int paren_depth = 0;

        while (*p != '\0' && *p != ';') {
            if (*p == '"') {
                in_quote = !in_quote;
            } else if (!in_quote) {
                if (*p == '(' || *p == '{' || *p == '[')
                    paren_depth++;
                else if (*p == ')' || *p == '}' || *p == ']')
                    paren_depth--;
                else if (*p == ',' && paren_depth == 0) {
                    p++;
                    break;
                }
            }
            if (a_len < (int)sizeof(arg_buf) - 1) {
                arg_buf[a_len++] = *p;
            }
            p++;
        }
        arg_buf[a_len] = '\0';

        char* a_start = arg_buf;
        while (*a_start == ' ') a_start++;
        int a_end = (int)strlen(a_start);
        while (a_end > 0 &&
               (a_start[a_end - 1] == ' ' || a_start[a_end - 1] == ';')) {
            a_start[--a_end] = '\0';
        }

        if (!first) {
            print_char(' ');
        }
        first = 0;

        struct as_value val = as_eval_expression(env, a_start);
        char out_buf[AS_LINE_MAX];
        as_value_format(&val, out_buf, sizeof(out_buf), 0);
        print(out_buf);
        as_value_free(&val);

        while (*p == ' ') p++;
    }

    print_char('\n');
}

/* Execute an "input" statement: reads a line from the user into a variable. */
static void as_exec_input(struct as_env* env, const char* stmt) {
    const char* p = stmt + 5;
    while (*p == ' ') p++;

    char prompt[64] = "";
    if (*p == '"') {
        p++;
        int pr_len = 0;
        while (*p != '\0' && *p != '"') {
            if (pr_len < (int)sizeof(prompt) - 1) {
                prompt[pr_len++] = *p;
            }
            p++;
        }
        prompt[pr_len] = '\0';
        if (*p == '"') p++;
        while (*p == ' ' || *p == ',') p++;
    }

    char var_name[AS_NAME_MAX];
    int v_len = 0;
    while (*p != '\0' && *p != ' ' && *p != ';') {
        if (v_len < (int)sizeof(var_name) - 1) {
            var_name[v_len++] = *p;
        }
        p++;
    }
    var_name[v_len] = '\0';

    if (prompt[0] != '\0') {
        print(prompt);
    }

    char input_line[SHELL_LINE_MAX];
    shell_read_line(input_line);

    struct as_var* var = as_env_lookup(env, var_name);
    if (var == 0) {
        var = as_env_define(env, var_name, AS_TYPE_STRING, AS_TYPE_VOID);
    }

    if (var != 0) {
        struct as_value v;
        v.type = AS_TYPE_STRING;
        v.elem_type = AS_TYPE_VOID;
        v.is_null = 0;
        v.u.str = as_strdup(input_line);
        as_value_cast_assign(&var->val, &v);
        as_value_free(&v);
    }
}

/* Execute an assignment statement: <var> = <expr> or <var>[<idx>] = <expr>. */
static void as_exec_assignment(struct as_env* env, const char* stmt) {
    const char* eq = strchr(stmt, '=');
    if (eq == 0) return;

    char lhs[64];
    int l_len = (int)(eq - stmt);
    if (l_len >= (int)sizeof(lhs)) l_len = (int)sizeof(lhs) - 1;
    memcpy(lhs, stmt, l_len);
    lhs[l_len] = '\0';

    char* l_start = lhs;
    while (*l_start == ' ') l_start++;
    int l_end = (int)strlen(l_start);
    while (l_end > 0 && l_start[l_end - 1] == ' ') {
        l_start[--l_end] = '\0';
    }

    const char* rhs = eq + 1;
    while (*rhs == ' ') rhs++;

    char rhs_buf[AS_LINE_MAX];
    strncpy(rhs_buf, rhs, sizeof(rhs_buf) - 1);
    rhs_buf[sizeof(rhs_buf) - 1] = '\0';
    int r_len = (int)strlen(rhs_buf);
    while (r_len > 0 &&
           (rhs_buf[r_len - 1] == ' ' || rhs_buf[r_len - 1] == ';')) {
        rhs_buf[--r_len] = '\0';
    }

    struct as_value val = as_eval_expression(env, rhs_buf);

    char* lbrack = strchr(l_start, '[');
    if (lbrack != 0) {
        *lbrack = '\0';
        char* rbrack = strchr(lbrack + 1, ']');
        if (rbrack != 0) *rbrack = '\0';

        struct as_var* var = as_env_lookup(env, l_start);
        if (var == 0) {
            kprintf("Error: variable '%s' not found.\n", l_start);
            env->error = 1;
            as_value_free(&val);
            return;
        }
        if (var->val.type != AS_TYPE_ARRAY) {
            kprintf("Error: '%s' is not an array.\n", l_start);
            env->error = 1;
            as_value_free(&val);
            return;
        }

        struct as_value idx_val = as_eval_expression(env, lbrack + 1);
        int idx = as_value_to_i32(&idx_val);
        as_value_free(&idx_val);

        if (idx < 0 || idx >= var->val.u.arr.count) {
            kprintf("Error: array index %d out of bounds for '%s'.\n", idx,
                    l_start);
            env->error = 1;
            as_value_free(&val);
            return;
        }

        as_value_cast_assign(&var->val.u.arr.items[idx], &val);
    } else {
        struct as_var* var = as_env_lookup(env, l_start);
        if (var == 0) {
            /* Auto-define variable if not defined yet. */
            var = as_env_define(env, l_start, val.type, val.elem_type);
        }
        if (var != 0) {
            as_value_cast_assign(&var->val, &val);
        }
    }

    as_value_free(&val);
}

/* Execute a block of lines between start and end (inclusive). */
static int as_exec_block(struct as_env* env, struct as_line* lines, int start,
                         int end) {
    int i = start;
    while (i <= end) {
        if (env->exit_flag || env->break_flag || env->continue_flag) {
            return 0;
        }

        const char* stmt = lines[i].text;
        while (*stmt == ' ') stmt++;

        if (*stmt == '\0') {
            i++;
            continue;
        }

        if (strcmp(stmt, "break") == 0 || strcmp(stmt, "break;") == 0) {
            env->break_flag = 1;
            return 0;
        }
        if (strcmp(stmt, "continue") == 0 || strcmp(stmt, "continue;") == 0) {
            env->continue_flag = 1;
            return 0;
        }
        if (strcmp(stmt, "exit") == 0 || strcmp(stmt, "exit;") == 0 ||
            strcmp(stmt, "return") == 0 || strcmp(stmt, "return;") == 0) {
            env->exit_flag = 1;
            return 0;
        }

        if (strncmp(stmt, "define ", 7) == 0) {
            as_exec_define(env, stmt);
            i++;
            continue;
        }

        if (strncmp(stmt, "delete ", 7) == 0) {
            as_exec_delete(env, stmt);
            i++;
            continue;
        }

        if (strncmp(stmt, "print", 5) == 0 &&
            (stmt[5] == ' ' || stmt[5] == ';' || stmt[5] == '\0')) {
            as_exec_print(env, stmt);
            i++;
            continue;
        }

        if (strncmp(stmt, "input ", 6) == 0) {
            as_exec_input(env, stmt);
            i++;
            continue;
        }

        if (strncmp(stmt, "for ", 4) == 0) {
            int done_line = as_find_matching_done(lines, end + 1, i);
            if (done_line < 0) {
                kprintf("Error on line %d: 'for' without matching 'done'.\n",
                        lines[i].orig_line);
                env->error = 1;
                return -1;
            }

            const char* fp = stmt + 4;
            while (*fp == ' ') fp++;

            char var_name[AS_NAME_MAX];
            int vn_len = 0;
            while (*fp != '\0' && *fp != ' ' && *fp != ':') {
                if (vn_len < (int)sizeof(var_name) - 1) {
                    var_name[vn_len++] = *fp;
                }
                fp++;
            }
            var_name[vn_len] = '\0';
            while (*fp == ' ') fp++;

            if (strncmp(fp, "in ", 3) == 0) {
                /* Array iteration loop. */
                fp += 3;
                while (*fp == ' ') fp++;

                char arr_name[AS_NAME_MAX];
                int an_len = 0;
                while (*fp != '\0' && *fp != ' ' && *fp != ':' && *fp != ';') {
                    if (an_len < (int)sizeof(arr_name) - 1) {
                        arr_name[an_len++] = *fp;
                    }
                    fp++;
                }
                arr_name[an_len] = '\0';

                struct as_var* arr_var = as_env_lookup(env, arr_name);
                if (arr_var == 0 || arr_var->val.type != AS_TYPE_ARRAY) {
                    kprintf("Error on line %d: '%s' is not an array.\n",
                            lines[i].orig_line, arr_name);
                    env->error = 1;
                    i = done_line + 1;
                    continue;
                }

                struct as_var* iter_var = as_env_lookup(env, var_name);
                if (iter_var == 0) {
                    iter_var = as_env_define(
                        env, var_name, arr_var->val.elem_type, AS_TYPE_VOID);
                }

                int total_elems = arr_var->val.u.arr.count;
                for (int k = 0; k < total_elems; k++) {
                    as_value_copy(&iter_var->val, &arr_var->val.u.arr.items[k]);
                    as_exec_block(env, lines, i + 1, done_line - 1);

                    if (env->exit_flag) break;
                    if (env->break_flag) {
                        env->break_flag = 0;
                        break;
                    }
                    env->continue_flag = 0;
                }

                i = done_line + 1;
                continue;
            } else {
                struct as_var* iter_var = as_env_lookup(env, var_name);
                if (iter_var == 0) {
                    iter_var = as_env_define(env, var_name, AS_TYPE_INT32,
                                             AS_TYPE_VOID);
                }

                if (strncmp(fp, "from ", 5) == 0) {
                    fp += 5;
                    while (*fp == ' ') fp++;
                    char start_expr[64];
                    int s_len = 0;
                    while (*fp != '\0' && strncmp(fp, "to ", 3) != 0) {
                        if (s_len < (int)sizeof(start_expr) - 1) {
                            start_expr[s_len++] = *fp;
                        }
                        fp++;
                    }
                    start_expr[s_len] = '\0';
                    struct as_value s_val = as_eval_expression(env, start_expr);
                    as_value_cast_assign(&iter_var->val, &s_val);
                    as_value_free(&s_val);
                }

                if (strncmp(fp, "to ", 3) == 0) {
                    fp += 3;
                    while (*fp == ' ') fp++;
                }

                char end_expr[64];
                int e_len = 0;
                while (*fp != '\0' && strncmp(fp, "step ", 5) != 0 &&
                       strncmp(fp, "do", 2) != 0) {
                    if (e_len < (int)sizeof(end_expr) - 1) {
                        end_expr[e_len++] = *fp;
                    }
                    fp++;
                }
                end_expr[e_len] = '\0';

                int step = 1;
                if (strncmp(fp, "step ", 5) == 0) {
                    fp += 5;
                    while (*fp == ' ') fp++;
                    char step_expr[64];
                    int st_len = 0;
                    while (*fp != '\0' && strncmp(fp, "do", 2) != 0) {
                        if (st_len < (int)sizeof(step_expr) - 1) {
                            step_expr[st_len++] = *fp;
                        }
                        fp++;
                    }
                    step_expr[st_len] = '\0';
                    struct as_value st_val = as_eval_expression(env, step_expr);
                    step = as_value_to_i32(&st_val);
                    as_value_free(&st_val);
                }

                struct as_value end_val = as_eval_expression(env, end_expr);
                int32_t target_end = as_value_to_i32(&end_val);
                as_value_free(&end_val);

                while (1) {
                    int32_t cur = as_value_to_i32(&iter_var->val);
                    if (step > 0 && cur > target_end) break;
                    if (step < 0 && cur < target_end) break;
                    if (step == 0) break;

                    as_exec_block(env, lines, i + 1, done_line - 1);

                    if (env->exit_flag) break;
                    if (env->break_flag) {
                        env->break_flag = 0;
                        break;
                    }
                    env->continue_flag = 0;

                    struct as_value next_val;
                    as_value_init(&next_val, AS_TYPE_INT32, AS_TYPE_VOID);
                    next_val.u.i32 = cur + step;
                    as_value_cast_assign(&iter_var->val, &next_val);
                }

                i = done_line + 1;
                continue;
            }
        }

        if (strncmp(stmt, "while ", 6) == 0) {
            int done_line = as_find_matching_done(lines, end + 1, i);
            if (done_line < 0) {
                kprintf("Error on line %d: 'while' without matching 'done'.\n",
                        lines[i].orig_line);
                env->error = 1;
                return -1;
            }

            const char* wp = stmt + 6;
            while (*wp == ' ') wp++;

            char cond_expr[AS_LINE_MAX];
            int c_len = 0;
            while (*wp != '\0' && strncmp(wp, "do", 2) != 0) {
                if (c_len < (int)sizeof(cond_expr) - 1) {
                    cond_expr[c_len++] = *wp;
                }
                wp++;
            }
            cond_expr[c_len] = '\0';

            while (1) {
                struct as_value cond = as_eval_expression(env, cond_expr);
                int truthy = as_value_is_truthy(&cond);
                as_value_free(&cond);
                if (!truthy) break;

                as_exec_block(env, lines, i + 1, done_line - 1);

                if (env->exit_flag) break;
                if (env->break_flag) {
                    env->break_flag = 0;
                    break;
                }
                env->continue_flag = 0;
            }

            i = done_line + 1;
            continue;
        }

        if (strncmp(stmt, "if ", 3) == 0) {
            int done_line = as_find_matching_done(lines, end + 1, i);
            if (done_line < 0) {
                kprintf("Error on line %d: 'if' without matching 'done'.\n",
                        lines[i].orig_line);
                env->error = 1;
                return -1;
            }

            int executed = 0;
            int branch_start = i;
            int depth = 1;

            for (int k = i + 1; k <= done_line; k++) {
                if (k < done_line) {
                    const char* kt = lines[k].text;
                    while (*kt == ' ') kt++;
                    if (strncmp(kt, "for ", 4) == 0 ||
                        strncmp(kt, "while ", 6) == 0 ||
                        strncmp(kt, "if ", 3) == 0) {
                        depth++;
                    } else if (strcmp(kt, "done") == 0 ||
                               strcmp(kt, "done;") == 0 ||
                               strcmp(kt, "endif") == 0 ||
                               strcmp(kt, "endif;") == 0 ||
                               strcmp(kt, "fi") == 0 ||
                               strcmp(kt, "fi;") == 0) {
                        depth--;
                    }
                }

                if (depth == 1 && (k == done_line ||
                                   strncmp(lines[k].text, "else if ", 8) == 0 ||
                                   strncmp(lines[k].text, "elif ", 5) == 0 ||
                                   strcmp(lines[k].text, "else") == 0 ||
                                   strcmp(lines[k].text, "else do") == 0 ||
                                   strcmp(lines[k].text, "else:") == 0)) {
                    if (!executed) {
                        const char* bt = lines[branch_start].text;
                        while (*bt == ' ') bt++;
                        int run_branch = 0;

                        if (strncmp(bt, "if ", 3) == 0 ||
                            strncmp(bt, "else if ", 8) == 0 ||
                            strncmp(bt, "elif ", 5) == 0) {
                            const char* cond_ptr =
                                bt +
                                (bt[0] == 'i' ? 3 : (bt[2] == 's' ? 8 : 5));
                            while (*cond_ptr == ' ') cond_ptr++;
                            char cond_buf[AS_LINE_MAX];
                            int cb_len = 0;
                            while (*cond_ptr != '\0' &&
                                   strncmp(cond_ptr, "do", 2) != 0 &&
                                   strncmp(cond_ptr, "then", 4) != 0) {
                                if (cb_len < (int)sizeof(cond_buf) - 1) {
                                    cond_buf[cb_len++] = *cond_ptr;
                                }
                                cond_ptr++;
                            }
                            cond_buf[cb_len] = '\0';
                            struct as_value c_val =
                                as_eval_expression(env, cond_buf);
                            run_branch = as_value_is_truthy(&c_val);
                            as_value_free(&c_val);
                        } else {
                            run_branch = 1;
                        }

                        if (run_branch) {
                            as_exec_block(env, lines, branch_start + 1, k - 1);
                            executed = 1;
                        }
                    }

                    branch_start = k;
                }
            }

            i = done_line + 1;
            continue;
        }

        const char* eq_ptr = strchr(stmt, '=');
        if (eq_ptr != 0 && (eq_ptr == stmt || *(eq_ptr - 1) != '=') &&
            *(eq_ptr + 1) != '=' && *(eq_ptr - 1) != '!' &&
            *(eq_ptr - 1) != '<' && *(eq_ptr - 1) != '>') {
            as_exec_assignment(env, stmt);
            i++;
            continue;
        }

        as_exec_system_cmd(env, stmt);
        i++;
    }

    return 0;
}

/* Run an AdaScript string. */
int adascript_run_string(const char* script) {
    if (script == 0 || script[0] == '\0') {
        return 0;
    }

    int line_count = 0;
    for (int i = 0; script[i] != '\0'; i++) {
        if (script[i] == '\n') {
            line_count++;
        }
    }
    line_count += 2;

    struct as_line* lines =
        (struct as_line*)kmalloc(line_count * sizeof(struct as_line));
    if (lines == 0) {
        print("Error: out of memory.\n");
        return -1;
    }

    char* script_copy = as_strdup(script);
    if (script_copy == 0) {
        kfree(lines);
        print("Error: out of memory.\n");
        return -1;
    }

    int total = 0;
    int curr_orig_line = 1;
    char* cur = script_copy;

    while (*cur != '\0') {
        char* line_start = cur;
        while (*cur != '\0' && *cur != '\n') {
            cur++;
        }
        if (*cur == '\n') {
            *cur = '\0';
            cur++;
        }

        int in_q = 0;
        for (int j = 0; line_start[j] != '\0'; j++) {
            if (line_start[j] == '"') {
                in_q = !in_q;
            } else if (!in_q) {
                if (line_start[j] == '#' ||
                    (line_start[j] == '/' && line_start[j + 1] == '/') ||
                    (line_start[j] == '-' && line_start[j + 1] == '-')) {
                    line_start[j] = '\0';
                    break;
                }
            }
        }

        int indent = 0;
        while (*line_start == ' ' || *line_start == '\t') {
            if (*line_start == ' ')
                indent++;
            else
                indent += 4;
            line_start++;
        }

        int len = (int)strlen(line_start);
        while (len > 0 &&
               (line_start[len - 1] == ' ' || line_start[len - 1] == '\r' ||
                line_start[len - 1] == '\t')) {
            line_start[--len] = '\0';
        }

        if (len > 0 && line_start[len - 1] == ';') {
            line_start[--len] = '\0';
            while (len > 0 && (line_start[len - 1] == ' ' ||
                               line_start[len - 1] == '\t')) {
                line_start[--len] = '\0';
            }
        }

        if (len > 0) {
            lines[total].text = line_start;
            lines[total].orig_line = curr_orig_line;
            lines[total].indent = indent;
            total++;
        }

        curr_orig_line++;
    }

    struct as_env env;
    env.vars = 0;
    env.error = 0;
    env.exit_flag = 0;
    env.break_flag = 0;
    env.continue_flag = 0;

    int result = as_exec_block(&env, lines, 0, total - 1);

    as_env_cleanup(&env);
    kfree(script_copy);
    kfree(lines);

    return result == 0 && env.error == 0 ? 0 : -1;
}

/* Run an AdaScript file at the given filesystem path. */
int adascript_run_file(const char* path) {
    struct fs_node* node = fs_resolve(path);
    if (node == 0) {
        return -1;
    }
    if (node->kind == FS_DIR) {
        print("Error: that path is a directory.\n");
        return -1;
    }

    return adascript_run_string(node->data);
}
