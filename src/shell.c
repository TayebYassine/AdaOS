#include "shell.h"

#include "keyboard.h"
#include "screen.h"

#define LINE_MAX 128 /* Longest line the user can type */

/*
 * Read one line from the keyboard. The characters appear on the screen as
 * they are typed and are also stored in a buffer. Pressing Enter finishes
 * the line: we print it back, just like the echo command.
 */
static void read_line(void) {
    char line[LINE_MAX];
    int length = 0;

    for (;;) {
        char c = keyboard_getchar();

        // Enter: terminate the string, move down, echo the line back.
        if (c == '\n') {
            line[length] = '\0';
            print_char('\n');
            print(line);
            print_char('\n');
            return;
        }

        // Backspace: drop the last character and erase it from the screen.
        if (c == '\b') {
            if (length > 0) {
                length--;
                print_char('\b');
            }
            continue;
        }

        // A normal key: store it and show it right away.
        if (length < LINE_MAX - 1) {
            line[length] = c;
            length++;
            print_char(c);
        }

        // Otherwise the line is full - ignore the extra keys.
    }
}

void shell_run(void) {
    print("Welcome to AdaOS Shell!\n\n");

    for (;;) {
        print("> ");
        read_line();
    }
}
