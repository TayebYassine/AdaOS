#include "setup.h"

#include <stdint.h>

#include "clock.h"
#include "diskfs.h"
#include "keyboard.h"
#include "libc.h"
#include "power.h"
#include "screen.h"
#include "timer.h"

// First-boot setup: the program that runs once, on a disk that has never
// been prepared, before the shell exists.
//
// It shows the welcome screen, waits three seconds, and then lets the user
// choose: ENTER sets the machine up, ESC turns it off. Setup asks what to
// prepare - the disk has to be formatted, the sample files and the clock
// are optional - and every step that takes a while draws a progress bar on
// the row above the last one, with the job it is doing written above it.
// When the disk holds a file system the machine reboots, and the next boot
// goes straight to the shell.

#define COLOR_TEXT 0x0F
#define COLOR_HINT 0x07

#define WELCOME_ROW 12
#define HINT_ROW 14

#define PROGRESS_ROW (SCREEN_ROWS - 2)
#define BAR_WIDTH 30

#define WELCOME_PAUSE_MS 3000
#define SHUTDOWN_PAUSE_MS 3000
#define BAR_PAUSE_MS 150

typedef int (*setup_work)(void (*progress)(int percent));

/* Write "text" centred on "row"; a row of the screen, not of the cursor. */
static void center_at(int row, const char* text, uint8_t attribute) {
    int col = (SCREEN_COLS - (int)strlen(text)) / 2;

    if (col < 0) {
        col = 0;
    }

    screen_puts_at(row, col, text, attribute);
}

/* Draw the progress bar with "percent" done; a negative percentage removes
 * it. The cursor is left alone, so the text above keeps growing below the
 * last line that was printed. */
static void draw_bar(int percent) {
    char number[12];
    char label[16];
    char inside[BAR_WIDTH + 1];
    int filled;
    int length;
    int at;
    int col;

    screen_fill_row(PROGRESS_ROW, ' ', COLOR_TEXT);

    if (percent < 0) {
        return;
    }

    if (percent > 100) {
        percent = 100;
    }

    filled = percent * BAR_WIDTH / 100;

    for (int i = 0; i < BAR_WIDTH; i++) {
        inside[i] = (i < filled) ? '=' : ' ';
    }
    inside[BAR_WIDTH] = '\0';

    utoa((uint32_t)percent, number);
    strcpy(label, number);
    strcat(label, "%");
    length = (int)strlen(label);

    at = (BAR_WIDTH - length) / 2;
    if (at < 0) {
        at = 0;
    }
    if (at + length > BAR_WIDTH) {
        at = BAR_WIDTH - length;
    }

    memcpy(inside + at, label, (size_t)length);

    col = (SCREEN_COLS - (BAR_WIDTH + 2)) / 2;

    screen_put_at(PROGRESS_ROW, col, '[', COLOR_TEXT);
    for (int i = 0; i < BAR_WIDTH; i++) {
        screen_put_at(PROGRESS_ROW, col + 1 + i, inside[i], COLOR_TEXT);
    }
    screen_put_at(PROGRESS_ROW, col + BAR_WIDTH + 1, ']', COLOR_TEXT);
}

/* Handed to the step being run, which calls it as it works. */
static void progress_update(int percent) { draw_bar(percent); }

/* Run one step: print what it is doing, show the bar while it works, then
 * take the bar away and report whether it worked. */
static int run_operation(const char* title, setup_work work) {
    int ok;

    print(title);
    print("\n");

    draw_bar(0);
    timer_sleep(BAR_PAUSE_MS);

    ok = work(progress_update);

    if (ok) {
        draw_bar(100);
        timer_sleep(BAR_PAUSE_MS);
    }

    draw_bar(-1);

    if (ok) {
        print("Done.\n\n");
    }

    return ok;
}

/* Ask a yes/no question and return 1 for Y, 0 for N, -1 for ESC. Other keys
 * are ignored, so a mistyped answer cannot skip a step. */
static int ask_yes_no(const char* question) {
    print(question);

    for (;;) {
        int key = keyboard_getchar();

        if (key == 'y' || key == 'Y') {
            print("Y\n");
            return 1;
        }

        if (key == 'n' || key == 'N') {
            print("N\n");
            return 0;
        }

        if (key == KEY_ESC) {
            print("\n");
            return -1;
        }
    }
}

/* A step the machine cannot do without was turned down. */
static void refuse_required(void) __attribute__((noreturn));
static void refuse_required(void) {
    print("\nThis operation is required but it has been denied, ");
    print("shutting down...\n");

    timer_sleep(SHUTDOWN_PAUSE_MS);
    power_shutdown();
}

/* Read one line of text, echoing it as it is typed; ESC gives up. Returns 1
 * when a line was finished, 0 when the user pressed ESC. */
static int read_line(char* out, int max) {
    int length = 0;

    out[0] = '\0';

    for (;;) {
        int key = keyboard_getchar();

        if (key == '\n') {
            out[length] = '\0';
            print_char('\n');
            return 1;
        }

        if (key == KEY_ESC) {
            print_char('\n');
            return 0;
        }

        if (key == '\b') {
            if (length > 0) {
                length--;
                print_char('\b');
            }
            continue;
        }

        if (key >= ' ' && key < 127 && length < max - 1) {
            out[length] = (char)key;
            length++;
            print_char((char)key);
        }
    }
}

/* Read "count" digits at the start of "text". */
static int parse_number(const char* text, int count, int* value) {
    int result = 0;

    for (int i = 0; i < count; i++) {
        char c = text[i];

        if (c < '0' || c > '9') {
            return 0;
        }

        result = result * 10 + (c - '0');
    }

    *value = result;
    return 1;
}

/* Ask for the date and the time, in the two forms the clock understands. */
static void ask_date_and_time(void) {
    char line[16];
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;

    print("Enter the date [MM-DD-YYYY]: ");

    if (read_line(line, sizeof(line)) && strlen(line) == 10 && line[2] == '-' &&
        line[5] == '-' && parse_number(line, 2, &month) &&
        parse_number(line + 3, 2, &day) && parse_number(line + 6, 4, &year) &&
        clock_set_date(year, month, day)) {
        print("Date set.\n");
    } else {
        print("The date was not changed.\n");
    }

    print("Enter the time [HH:MM:SS]: ");

    if (read_line(line, sizeof(line)) && strlen(line) == 8 && line[2] == ':' &&
        line[5] == ':' && parse_number(line, 2, &hour) &&
        parse_number(line + 3, 2, &minute) && parse_number(line + 6, 2, &second) &&
        clock_set_time(hour, minute, second)) {
        print("Time set.\n\n");
    } else {
        print("The time was not changed.\n\n");
    }
}

/* Lay an empty, freshly formatted file system on the disk. */
static int format_step(void (*progress)(int)) {
    return diskfs_format_disk(0, progress);
}

/* Put the files the boot loaded into RAM - the sample tree - on the disk. */
static int samples_step(void (*progress)(int)) {
    return diskfs_write_tree(progress);
}

/* Show the welcome screen and wait for ENTER or ESC. Returns 1 for ENTER,
 * and never returns at all when the user wants the machine off. */
static int wait_for_choice(void) {
    screen_clear();
    center_at(WELCOME_ROW, "Welcome to AdaOS!", COLOR_TEXT);

    timer_sleep(WELCOME_PAUSE_MS);

    center_at(HINT_ROW, "Press ENTER to setup, ESC to exit", COLOR_HINT);

    for (;;) {
        int key = keyboard_getchar();

        if (key == '\n') {
            return 1;
        }

        if (key == KEY_ESC) {
            power_shutdown();
        }
    }
}

void setup_run_if_needed(void) {
    int accept;

    if (!diskfs_probe() || !diskfs_blank()) {
        return;
    }

    wait_for_choice();

    screen_clear();
    print("Welcome to AdaOS Setup!\n\n");

    accept = ask_yes_no("Would you like to format this disk [Y/N]? ");
    if (accept != 1) {
        refuse_required();
    }

    if (!run_operation("Formatting the disk...", format_step)) {
        print("The disk could not be formatted.\n\n");
        print("Shutting down...\n");
        timer_sleep(SHUTDOWN_PAUSE_MS);
        power_shutdown();
    }

    accept = ask_yes_no("Would you like to install the sample files [Y/N]? ");
    if (accept == 1) {
        if (!run_operation("Installing the sample files...", samples_step)) {
            print("The sample files could not be written.\n\n");
        }
    }

    accept = ask_yes_no("Would you like to set the date and time [Y/N]? ");
    if (accept == 1) {
        ask_date_and_time();
    }

    print("Setup is done, rebooting...\n");
    timer_sleep(SHUTDOWN_PAUSE_MS);
    power_reboot();
}
