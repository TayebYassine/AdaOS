#ifndef ADAOS_COMMAND_H
#define ADAOS_COMMAND_H

/* Execute one command line typed by the user. */
void command_execute(char* line);

/* Run a command from AdaScript, collecting what it would have shown on the
 * screen instead of drawing it. Returns 1 on success, 0 when the command
 * reported an error. "output" receives a heap string the caller must free;
 * on failure it holds the error text. */
int command_run_capture(const char* line, char** output);

/* Does this command name produce text that can be used as a string? */
int command_returns_output(const char* name);

#endif /* ADAOS_COMMAND_H */
