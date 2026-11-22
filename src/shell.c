#include "shell.h"

#include "command.h"
#include "keyboard.h"
#include "libc.h"
#include "screen.h"

#define LINE_MAX 128     /* Longest line the user can type */
#define HISTORY_SIZE 8   /* Number of past lines to remember */

static char history[HISTORY_SIZE][LINE_MAX];
static int history_count = 0; /* Entries currently stored (0..HISTORY_SIZE) */
static int history_start = 0; /* Where the oldest entry sits when full */

/* The i-th entry, 0 = oldest. */
static char* history_at(int i) {
    return history[(history_start + i) % HISTORY_SIZE];
}

/* Remember a line the user typed (called on Enter). */
static void history_push(const char* line) {
    // Don't remember the same line twice in a row.
    if (history_count > 0 && strcmp(line, history_at(history_count - 1)) == 0) {
        return;
    }

    if (history_count == HISTORY_SIZE) {
        // Full: drop the oldest line and reuse its slot.
        history_start = (history_start + 1) % HISTORY_SIZE;
    } else {
        history_count++;
    }

    memcpy(history_at(history_count - 1), line, strlen(line) + 1);
}

/*
 * Read one line from the keyboard. The characters appear on the screen as
 * they are typed and are also stored in the buffer. The Up/Down arrows
 * walk through the command history, Enter finishes the line: we move to
 * a new line and hand the buffer to command_execute().
 */
static void read_line(char* line) {
    int length = 0;
    int view = history_count;   // browsing position: history_count = new line
    char fresh[LINE_MAX];       // what was typed before browsing started
    int fresh_length = 0;

    for (;;) {
        int key = keyboard_getchar();

        // Enter: terminate the string and move down. The typed text is
        // already on the screen, so there is nothing left to print.
        if (key == '\n') {
            line[length] = '\0';
            print_char('\n');
            return;
        }

        // Backspace: drop the last character and erase it from the screen.
        if (key == '\b') {
            if (length > 0) {
                length--;
                print_char('\b');
            }
            continue;
        }

        // Up/Down arrows: replace the text after the prompt with a line
        // from the history (Up = older, Down = newer).
        if (key == KEY_UP || key == KEY_DOWN) {
            if (key == KEY_UP && view > 0) {
                if (view == history_count) {
                    // First step back: remember the line being typed.
                    memcpy(fresh, line, (size_t)length);
                    fresh_length = length;
                }
                view--;
            } else if (key == KEY_DOWN && view < history_count) {
                view++;
            } else {
                continue; // nothing to show in that direction
            }

            // Erase the current text after the prompt.
            for (int i = 0; i < length; i++) {
                print_char('\b');
            }

            if (view == history_count) {
                // Past the newest entry: restore the line being typed.
                memcpy(line, fresh, (size_t)fresh_length);
                length = fresh_length;
            } else {
                memcpy(line, history_at(view), strlen(history_at(view)) + 1);
                length = (int)strlen(line);
            }

            // The restore above may have copied fewer bytes than the old
            // line held, so its '\0' would sit too far back: re-terminate.
            line[length] = '\0';

            print(line);
            continue;
        }

        // A normal key (arrows are 0x100+, so they never land here):
        // store it and show it right away.
        if (key < KEY_UP) {
            if (length < LINE_MAX - 1) {
                line[length] = (char)key;
                length++;
                print_char((char)key);
            }

            // Otherwise the line is full - ignore the extra keys.
        }
    }
}

void shell_run(void) {
    char line[LINE_MAX];

    print("Welcome to AdaOS Shell!\n\n");
    print("Type 'help' to see the available commands.\n\n");

    // Like MS-DOS: the prompt comes back after every line, forever.
    for (;;) {
        print("> ");
        read_line(line);

        if (line[0] != '\0') {
            history_push(line);
        }

        command_execute(line);
    }
}
