#ifndef ADAOS_COMMAND_H
#define ADAOS_COMMAND_H

// The shell's command dispatcher: it splits a typed line into words, resolves
// the first word to a built-in, and hands it the remaining arguments plus the
// whole line. It also offers the same commands to AdaScript, which needs their
// text instead of their screen output.

/* Execute one command line typed by the user. */
void command_execute(char* line);

/* Run a command and collect its screen output into a heap string instead of
 * drawing it; returns 1 on success, 0 when the command reported an error. */
int command_run_capture(const char* line, char** output);

/* Does this command name produce text that can be used as a string? */
int command_returns_output(const char* name);

#endif /* ADAOS_COMMAND_H */
