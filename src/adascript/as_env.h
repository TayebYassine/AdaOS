#ifndef ADAOS_ADASCRIPT_ENV_H
#define ADAOS_ADASCRIPT_ENV_H

#include "as_value.h"

// The AdaScript environment: the variables a running script can see, kept in a
// linked list with the newest first, plus the flags that carry a break,
// continue, return or error out of a block. The environment is built for one
// run, so scripts do not leak variables into each other.

/* The mode a script declares on its first line. It fixes which statements the
 * script may use: screen scripts draw cells, CLI scripts read and print. */
enum as_script_mode {
    AS_MODE_NONE = 0,  // no mode has been declared
    AS_MODE_SCREEN,
    AS_MODE_CLI,
};

/* One variable in RAM. */
struct as_var {
    char name[AS_NAME_MAX];
    struct as_value val;
    struct as_var* next;  // next variable in the environment
};

/* Execution environment and symbol table. */
struct as_env {
    struct as_var* vars;  // symbol table, newest first
    int error;
    int exit_flag;
    int break_flag;
    int continue_flag;
    int loop_depth;
    int block_depth;  // blocks running right now, to bound the recursion
    int error_line;  // where the error happened
    int error_col;
    char error_text[AS_LINE_MAX];  // and how to explain it
    int cur_line;                  // the statement being run, kept up
    int cur_col;                   // to date by as_exec_block()
    enum as_script_mode script_mode;  // mode declared by the first statement
    int screen_mode;  // the run owns the screen and must give it back
    uint16_t* saved_cells;  // the shell's text cells while it does
    int saved_row;          // and the cursor, top row and cursor visibility
    int saved_col;
    int saved_top;
};

/* Find a variable by name in the environment. */
struct as_var* as_env_lookup(struct as_env* env, const char* name);

/* Register a variable; an existing one is re-initialised, so a `define`
 * inside a loop or an `if` body does not fail on its second pass. */
struct as_var* as_env_define(struct as_env* env, const char* name,
                             enum as_type type, enum as_type elem_type);

/* Unregister a variable from RAM and free its memory. */
int as_env_delete(struct as_env* env, const char* name);

/* Free all variables remaining in RAM. */
void as_env_cleanup(struct as_env* env);

/* Parse type name string like "int8", "string", "uint32[]". */
int as_parse_type(const char* s, enum as_type* out_type,
                  enum as_type* out_elem);

#endif /* ADAOS_ADASCRIPT_ENV_H */
