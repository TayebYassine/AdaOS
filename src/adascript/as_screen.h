#ifndef ADAOS_ADASCRIPT_SCREEN_H
#define ADAOS_ADASCRIPT_SCREEN_H

#include "as_env.h"

// Drawing statements for AdaScript. "mode screen" hands the whole 80x25 text
// screen to the script: the shell's cells, cursor and clock bar are saved and
// put back when the run ends, even after an error. "writecell" draws text at
// one cell with an optional colour and "erasecell" clears one cell or the
// rectangle between two corners. "mode cli" gives the screen back early.

/* Parse a "mode <name>" statement. Returns 1 and sets *mode for a known mode,
 * 0 when the line is not a mode statement, and -1 when the name is unknown.
 * When name is not null it points at the name text. */
int as_parse_mode_line(const char* stmt, enum as_script_mode* mode,
                       const char** name);

/* Execute a "mode screen" or "mode cli" statement. */
void as_exec_mode(struct as_env* env, const char* stmt);

/* Execute a "writecell <row> <column> <text> [color <name>]" statement. */
void as_exec_writecell(struct as_env* env, const char* stmt);

/* Execute an "erasecell <row> <column> [<row> <column>]" statement. */
void as_exec_erasecell(struct as_env* env, const char* stmt);

/* Put the shell's screen back if the run still owns it, reprinting any error
 * on the restored screen. */
void as_screen_finish(struct as_env* env);

#endif /* ADAOS_ADASCRIPT_SCREEN_H */
