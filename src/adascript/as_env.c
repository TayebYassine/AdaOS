#include "as_env.h"

#include "kmalloc.h"
#include "libc.h"

/* Find a variable by name in the environment. */
struct as_var* as_env_lookup(struct as_env* env, const char* name) {
    for (struct as_var* v = env->vars; v != 0; v = v->next) {
        if (strcmp(v->name, name) == 0) {
            return v;
        }
    }
    return 0;
}

/* Register a variable; an existing one is re-initialised, so a `define`
 * inside a loop or an `if` body does not fail on its second pass. */
struct as_var* as_env_define(struct as_env* env, const char* name,
                             enum as_type type, enum as_type elem_type) {
    if (name[0] == '\0') {
        return 0;
    }

    struct as_var* existing = as_env_lookup(env, name);
    if (existing != 0) {
        as_value_free(&existing->val);
        as_value_init(&existing->val, type, elem_type);
        return existing;
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
int as_env_delete(struct as_env* env, const char* name) {
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
void as_env_cleanup(struct as_env* env) {
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
int as_parse_type(const char* s, enum as_type* out_type,
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
