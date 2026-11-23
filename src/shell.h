#ifndef ADAOS_SHELL_H
#define ADAOS_SHELL_H

/* Longest line the shell reads, ending '\0' included. */
#define SHELL_LINE_MAX 128

/* Run the shell. */
void shell_run(void);

/* Read one line from the keyboard: characters echo as typed, Left/Right move
 * the cursor, Up/Down walk the history, Enter finishes the line. "line" needs
 * room for SHELL_LINE_MAX bytes. */
void shell_read_line(char* line);

#endif /* ADAOS_SHELL_H */
