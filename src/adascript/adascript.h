#ifndef ADAOS_ADASCRIPT_H
#define ADAOS_ADASCRIPT_H

// AdaScript: a small interpreted language run by the shell. A script is a text
// file of one statement per line; variable definitions with optional types,
// assignments, print, input, if/else blocks, for and while loops, shell
// commands and, in screen mode, cell drawing with writecell and erasecell plus
// keepalive, which keeps the script running until it is stopped. Ctrl+E stops
// any running script at once, in either mode, and puts the screen back.
// Every script declares "mode screen" or "mode cli" on its first line: screen
// scripts draw cells, CLI scripts print and read, and a statement from the
// other mode stops the run with an error. The interpreter tokenizes each line,
// parses expressions with the usual precedence, keeps variables in an
// environment with declared types, and reports errors with the line and column
// where they happened. Values are integers, strings, booleans, null and arrays.
// Running a script builds its own environment, so scripts do not leak variables
// into each other.

/* Run an AdaScript file (.as) at the given filesystem path; 0 on success. */
int adascript_run_file(const char* path);

/* Run an AdaScript passed as a null-terminated text string; 0 on success. */
int adascript_run_string(const char* script);

/* Is an AdaScript running right now? */
int adascript_is_running(void);

/* Has Ctrl+E asked the running script to stop? Always 0 outside a run. */
int adascript_interrupt_requested(void);

#endif /* ADAOS_ADASCRIPT_H */
