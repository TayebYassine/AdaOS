#include "setup.h"

#include <stdint.h>

#include "diskfs.h"
#include "keyboard.h"
#include "libc.h"
#include "power.h"
#include "screen.h"
#include "timer.h"

#define COLOR_TEXT 0x0F  // white text for the messages and the bar
#define COLOR_HINT 0x07  // grey text for the key hint

#define WELCOME_ROW 12
#define HINT_ROW 14

#define PROGRESS_ROW (SCREEN_ROWS - 2)  // the row above the last one
#define BAR_WIDTH 30                    // characters between the brackets

#define WELCOME_PAUSE_MS 3000
#define RAM_PAUSE_MS 3000
#define SHUTDOWN_PAUSE_MS 5000
#define BAR_PAUSE_MS 150

/* A setup step: it reports progress and returns 1 when it succeeded. */
typedef int (*setup_work)(void (*progress)(int percent));

/* Write "text" centred on "row"; a row of the screen, not of the cursor. */
static void center_at(int row, const char* text, uint8_t attribute) {
    int col = (SCREEN_COLS - (int)strlen(text)) / 2;

    if (col < 0) {
        col = 0;
    }

    screen_puts_at(row, col, text, attribute);
}

/* Draw the progress bar with "percent" done; a negative percentage removes it.
 * The cursor is left alone, so the text above keeps growing below it. */
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

    // The percentage sits in the middle of the bar.
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

/* Run one step: announce it, show the bar while it works, then report. */
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

/* Ask whether to install AdaOS or only try it: 1 for install, 2 for RAM. */
static int ask_mode(void) {
    print("Do you want to install AdaOS [1] or just try it out [2]? ");

    for (;;) {
        int key = keyboard_getchar();

        if (key == '1' || key == '2') {
            print_char((char)key);
            print_char('\n');
            return key - '0';
        }

        if (key == KEY_ESC) {
            print_char('\n');
            power_shutdown();
        }
    }
}

/* Ask a yes/no question: 1 for Y, 0 for N, -1 for ESC. */
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
    print("\nError: Formatting disk is required for setup.\n\n");
    print("Shutting down...\n");

    timer_sleep(SHUTDOWN_PAUSE_MS);
    power_shutdown();
}

/* Lay an empty, freshly formatted file system on the disk. */
static int format_step(void (*progress)(int)) {
    return diskfs_format_disk(0, progress);
}

/* Put the files the boot loaded into RAM - the sample tree - on the disk. */
static int samples_step(void (*progress)(int)) {
    return diskfs_write_tree(progress);
}

/* Show the welcome screen and return on ENTER; ESC powers the machine off. */
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

/* Offer the first-boot setup when the disk holds no file system yet. */
void setup_run_if_needed(void) {
    int accept;

    if (!diskfs_probe() || !diskfs_blank()) {
        return;
    }

    wait_for_choice();

    screen_clear();
    print("Welcome to AdaOS Setup!\n\n");

    if (ask_mode() == 2) {
        // Try it out: the tree loaded at boot stays in RAM and the disk is not
        // touched, so nothing survives the reboot.
        print("\nOkay, AdaOS's file manager will live in RAM for only this "
              "session.\n");

        diskfs_use_ram();
        timer_sleep(RAM_PAUSE_MS);
        screen_clear();
        return;
    }

    print("\n");

    accept = ask_yes_no("Would you like to format this disk [Y/N]? ");
    if (accept != 1) {
        refuse_required();
    }

    if (!run_operation("Formatting the disk...", format_step)) {
        print("Error: The disk could not be formatted.\n\n");
        print("Shutting down...\n");
        timer_sleep(SHUTDOWN_PAUSE_MS);
        power_shutdown();
    }

    accept = ask_yes_no("Would you like to install the sample files [Y/N]? ");
    if (accept == 1) {
        if (!run_operation("Installing the sample files...", samples_step)) {
            print("Error: The sample files could not be written.\n\n");
        }
    } else {
        print("Okay, sample files will not be installed.\n\n");
    }

    print("Congratulations! We're all set.\n");
    print("Rebooting in 5 seconds...\n");
    timer_sleep(SHUTDOWN_PAUSE_MS);
    power_reboot();
}
