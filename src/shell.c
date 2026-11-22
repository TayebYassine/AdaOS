#include "shell.h"

#include "command.h"
#include "keyboard.h"
#include "screen.h"

#define LINE_MAX 128 /* Longest line the user can type */

/*
 * Read one line from the keyboard. The characters appear on the screen as
 * they are typed and are also stored in the buffer. Pressing Enter finishes
 * the line: we move to a new line and hand the buffer to command_execute().
 */
static void read_line(char* line) {
    int length = 0;

    for (;;) {
        char c = keyboard_getchar();

        // Enter: terminate the string and move down. The typed text is
        // already on the screen, so there is nothing left to print.
        if (c == '\n') {
            line[length] = '\0';
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
    char line[LINE_MAX];

    print("Welcome to AdaOS Shell!\n\n");
    print("Type 'help' to see the available commands.\n\n");

    for (;;) {
        print("> ");
        read_line(line);
        command_execute(line);
    }
}
