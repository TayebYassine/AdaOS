#ifndef ADAOS_SHELL_H
#define ADAOS_SHELL_H

// The interactive shell: prints the prompt, reads a line from the keyboard with
// cursor editing and Up/Down history, and hands each finished line to the
// command dispatcher. Reading keeps the line in one buffer that callers
// provide, sized SHELL_LINE_MAX.

/* Longest line the shell reads, ending '\0' included. */
#define SHELL_LINE_MAX 128

/* Run the shell. */
void shell_run(void);

/* Read one line: characters echo as typed, Left/Right move the cursor,
 * Up/Down walk the history, Enter finishes it. "line" needs SHELL_LINE_MAX. */
void shell_read_line(char* line);

#endif /* ADAOS_SHELL_H */
