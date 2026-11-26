#include "shell.h"

#include "adascript.h"
#include "clock.h"
#include "command.h"
#include "keyboard.h"
#include "libc.h"
#include "screen.h"

#define HISTORY_SIZE 8

// History is a ring: history_start is the oldest of history_count entries.
static char history[HISTORY_SIZE][SHELL_LINE_MAX];
static int history_count = 0;
static int history_start = 0;

static char* history_at(int i) {
    return history[(history_start + i) % HISTORY_SIZE];
}

/* Remember a line the user typed, skipping an immediate repeat. */
static void history_push(const char* line) {
    if (history_count > 0 && strcmp(line, history_at(history_count - 1)) == 0) {
        return;
    }

    if (history_count == HISTORY_SIZE) {
        history_start = (history_start + 1) % HISTORY_SIZE;
    } else {
        history_count++;
    }

    memcpy(history_at(history_count - 1), line, strlen(line) + 1);
}

void shell_read_line(char* line) {
    int length = 0;
    int cursor = 0;
    int view = history_count;
    char fresh[SHELL_LINE_MAX];
    int fresh_length = 0;
    int fresh_cursor = 0;

    line[0] = '\0';

    for (;;) {
        // Ctrl+E stops a running script even while it is waiting for a line.
        if (adascript_interrupt_requested()) {
            line[0] = '\0';
            print_char('\n');
            return;
        }

        int key = keyboard_getchar();

        // Enter: terminate the string and move down.
        if (key == '\n') {
            line[length] = '\0';
            print_char('\n');
            return;
        }

        // Backspace: delete the character before the cursor.
        if (key == '\b') {
            if (cursor > 0) {
                // Shift the rest of the line one position left.
                for (int i = cursor; i <= length; i++) {
                    line[i - 1] = line[i];  // includes the '\0'
                }
                length--;
                cursor--;

                cursor_left();
                print(line + cursor);
                print_char(' ');
                for (int i = cursor; i <= length; i++) {
                    cursor_left();
                }
            }
            continue;
        }

        // Left/Right arrows: move the cursor inside the line.
        if (key == KEY_LEFT || key == KEY_RIGHT) {
            if (key == KEY_LEFT && cursor > 0) {
                cursor--;
                cursor_left();
            } else if (key == KEY_RIGHT && cursor < length) {
                cursor++;
                cursor_right();
            }
            continue;
        }

        // Up/Down: swap the line for one from the history (Up = older).
        if (key == KEY_UP || key == KEY_DOWN) {
            if (key == KEY_UP && view > 0) {
                // Keep the line being edited before stepping into history.
                if (view == history_count) {
                    memcpy(fresh, line, (size_t)length);
                    fresh_length = length;
                    fresh_cursor = cursor;
                }
                view--;
            } else if (key == KEY_DOWN && view < history_count) {
                view++;
            } else {
                continue;
            }

            // Move to the end of the current line, then erase it all.
            while (cursor < length) {
                cursor_right();
                cursor++;
            }

            for (int i = 0; i < length; i++) {
                print_char('\b');
            }

            if (view == history_count) {
                memcpy(line, fresh, (size_t)fresh_length);
                length = fresh_length;
                cursor = fresh_cursor;
            } else {
                memcpy(line, history_at(view), strlen(history_at(view)) + 1);
                length = (int)strlen(line);
                cursor = length;
            }

            line[length] = '\0';
            print(line);

            // A restored line may have had its cursor in the middle.
            for (int i = cursor; i < length; i++) {
                cursor_left();
            }
            continue;
        }

        // Normal key: insert it at the cursor and show it right away.
        if (key >= ' ' && key < KEY_UP) {
            if (length < SHELL_LINE_MAX - 1) {
                // Shift the rest of the line one position right.
                for (int i = length; i >= cursor; i--) {
                    line[i + 1] = line[i];  // includes the '\0'
                }
                line[cursor] = (char)key;
                length++;
                cursor++;

                // Show the shifted tail, then move the cursor back over it.
                print(line + cursor - 1);
                for (int i = cursor; i < length; i++) {
                    cursor_left();
                }
            }
        }
    }
}

void shell_run(void) {
    char line[SHELL_LINE_MAX];

    screen_set_top(1);
    clock_resume();

    print("Welcome to AdaOS!\n\n");
    print("Type 'help' to see the available commands.\n\n");

    for (;;) {
        print("> ");
        shell_read_line(line);

        if (line[0] != '\0') {
            history_push(line);
        }

        command_execute(line);
    }
}
