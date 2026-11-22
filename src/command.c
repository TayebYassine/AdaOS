#include "command.h"

#include "io.h"
#include "screen.h"

#include <stdint.h>

#define MAX_WORDS 32 /* Maximum number of words in one command line */

/* Number of elements in an array. */
#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))

/*
 * Command ids.
 */
enum command_id {
    CMD_PRINT = 0,
    CMD_HELP,
    CMD_SHUTDOWN,
    CMD_REBOOT,
    CMD_CLEAR,
    CMD_NOT_FOUND = -1,
};

static const char* command_names[] = { "print", "help", "shutdown", "reboot", "clear" };

static const char* command_help[] = {
    "print <text> - print text on the screen",
    "help         - show this list of commands",
    "shutdown     - turn off the computer",
    "reboot       - restart the computer",
    "clear        - clear the screen"
};

/* Compare two strings: 1 if they are equal, 0 otherwise. */
static int str_eq(const char* a, const char* b) {
    int i = 0;

    while (a[i] != '\0' && a[i] == b[i]) {
        i++;
    }

    return a[i] == b[i];
}

/* Look up a command by name; returns its id or CMD_NOT_FOUND. */
static int find_command(const char* name) {
    for (int i = 0; i < (int)ARRAY_COUNT(command_names); i++) {
        if (str_eq(name, command_names[i])) {
            return i;
        }
    }

    return CMD_NOT_FOUND;
}

/*
 * Split a line into words separated by spaces. The line is modified in
 * place: every space that ends a word becomes '\0', so each word becomes
 * its own string. The words are collected in words[] and the number of
 * words is returned.
 */
static int split(char* line, char* words[], int max) {
    int count = 0;
    int i = 0;

    while (line[i] == ' ') { // skip the spaces before the first word
        i++;
    }

    while (line[i] != '\0' && count < max) {
        words[count] = &line[i]; // the word starts here
        count++;

        while (line[i] != '\0' && line[i] != ' ') { // run to its end
            i++;
        }

        if (line[i] == ' ') {
            line[i] = '\0'; // cut the word off
            i++;

            while (line[i] == ' ') { // skip extra spaces between words
                i++;
            }
        }
    }

    return count;
}

/*
 * Power off the machine: ask the chipset to enter the ACPI "S5" (soft
 * off) state. Different machines listen on different ports, so we try
 * the known ones - if one works, the machine turns off right here.
 */
static void command_shutdown(void) {
    outw(0x604, 0x2000);  // QEMU / KVM
    outw(0xB004, 0x2000); // Bochs / older QEMU
    outw(0x4004, 0x0034); // VirtualBox

    print("It is now safe to turn off your computer.\n");

    for (;;) {
        __asm__ volatile("hlt");
    }
}

/*
 * Restart the machine. Two tricks, in order:
 * 1. Ask the keyboard controller to pulse the CPU's reset line (0xFE).
 * 2. If that fails, make the CPU crash on purpose: we load an empty
 *    interrupt table and trigger an interrupt. The CPU cannot find the
 *    handler, faults again while handling that, and finally resets
 *    itself ("triple fault").
 */
static void command_reboot(void) {
    // Wait until the keyboard controller is ready for a command:
    // bit 1 of port 0x64 is the "input buffer full" flag.
    while ((inb(0x64) & 0x02) != 0) {
        // still full, so keep waiting
    }

    outb(0x64, 0xFE); // pulse the CPU reset line

    // The controller ignored us, so force a triple fault
    struct {
        uint16_t limit;
        uint32_t base;
    } __attribute__((packed)) empty_idt = {0, 0};

    __asm__ volatile("lidt %0" : : "m"(empty_idt));
    __asm__ volatile("int $0");

    for (;;) { // never reached: the CPU resets instead
        __asm__ volatile("hlt");
    }
}

void command_execute(char* line) {
    char* words[MAX_WORDS];
    int count = split(line, words, MAX_WORDS);

    if (count == 0) {
        return;
    }

    int command = find_command(words[0]);

    switch (command) {
    case CMD_PRINT: {
        for (int i = 1; i < count; i++) {
            if (i > 1) {
                print_char(' ');
            }
            print(words[i]);
        }

        print_char('\n');
        break;
    }
    case CMD_HELP: {
        print("Available commands:\n");

        for (int i = 0; i < (int)ARRAY_COUNT(command_help); i++) {
            print("  ");
            print(command_help[i]);
            print_char('\n');
        }
        break;
    }
    case CMD_SHUTDOWN:
        command_shutdown();
        break;
    case CMD_REBOOT:
        command_reboot();
        break;
    case CMD_CLEAR:
        screen_clear();
        break;
    default:
        print("Error: command not found.\n");
        break;
    }
}
