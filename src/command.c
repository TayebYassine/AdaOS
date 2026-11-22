#include "command.h"

#include <stdint.h>

#include "io.h"
#include "kprintf.h"
#include "libc.h"
#include "pmm.h"
#include "screen.h"
#include "timer.h"

// The shell's command dispatcher: splits the typed line into words,
// looks the first one up, and runs the matching command.

/* Maximum number of words in one command line. */
#define MAX_WORDS 32
/* The longest sleep acceptable: 86400 seconds (24 hours). */
#define MAX_SLEEP_SECONDS 86400
/* Number of elements in an array. */
#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))

/* Command ids. */
enum command_id {
    CMD_PRINT = 0,
    CMD_HELP,
    CMD_SHUTDOWN,
    CMD_REBOOT,
    CMD_CLEAR,
    CMD_UPTIME,
    CMD_SLEEP,
    CMD_MEM,
    CMD_NOT_FOUND = -1,
};

static const char* command_names[] = {"print", "help",   "shutdown", "reboot",
                                      "clear", "uptime", "sleep",    "mem"};

static const char* command_help[] = {
    "print <text> - print text on the screen",
    "help         - show this list of commands",
    "shutdown     - turn off the computer",
    "reboot       - restart the computer",
    "clear        - clear the screen",
    "uptime       - how long AdaOS has been running",
    "sleep <sec>  - pause for a number of seconds",
    "mem          - show RAM, pages and where the kernel sits"};

/* Look up a command by name; returns its id or CMD_NOT_FOUND. */
static int find_command(const char* name) {
    for (int i = 0; i < (int)ARRAY_COUNT(command_names); i++) {
        if (strcmp(name, command_names[i]) == 0) {
            return i;
        }
    }

    return CMD_NOT_FOUND;
}

/* Split a line into words separated by spaces. */
static int split(char* line, char* words[], int max) {
    int count = 0;
    int i = 0;

    while (line[i] == ' ') {
        i++;
    }

    while (line[i] != '\0' && count < max) {
        words[count] = &line[i];
        count++;

        while (line[i] != '\0' && line[i] != ' ') {
            i++;
        }

        if (line[i] == ' ') {
            line[i] = '\0';
            i++;

            while (line[i] == ' ') {
                i++;
            }
        }
    }

    return count;
}

/* Print one number in base 10, padded with leading zeros up to "width" digits.
 */
static void print_padded(unsigned int value, int width) {
    char digits[10];  // enough for 4,294,967,295
    int count = 0;

    do {
        digits[count] = (char)('0' + (value % 10));
        count++;
        value /= 10;
    } while (value != 0);

    for (int i = count; i < width; i++) {
        print_char('0');
    }

    while (count > 0) {
        count--;
        print_char(digits[count]);
    }
}

/* Read a decimal number at the start of a word; returns the digit count (0 = no
 * number). */
static int parse_uint(const char* word, uint32_t* value) {
    uint32_t result = 0;
    int digits = 0;

    while (word[digits] >= '0' && word[digits] <= '9') {
        result = result * 10 + (uint32_t)(word[digits] - '0');
        digits++;
    }

    *value = result;
    return digits;
}

/* Power off the machine through the ACPI "S5" (soft off) state. */
static void command_shutdown(void) {
    outw(0x604, 0x2000);   // QEMU / KVM
    outw(0xB004, 0x2000);  // Bochs / older QEMU
    outw(0x4004, 0x0034);  // VirtualBox

    print("It is now safe to turn off your computer.\n");

    for (;;) {
        __asm__ volatile("hlt");
    }
}

/* Restart the machine. */
static void command_reboot(void) {
    // Wait for the controller to be ready: bit 1 of port 0x64 = input buffer
    // full.
    while ((inb(0x64) & 0x02) != 0) {
    }

    outb(0x64, 0xFE);  // pulse the CPU reset line

    // The controller ignored us: force a triple fault instead.
    struct {
        uint16_t limit;
        uint32_t base;
    } __attribute__((packed)) empty_idt = {0, 0};

    __asm__ volatile("lidt %0" : : "m"(empty_idt));
    __asm__ volatile("int $0");

    for (;;) {
        __asm__ volatile("hlt");
    }
}

/* Split the line into words and run the command the first one names. */
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
        case CMD_UPTIME: {
            uint32_t total = timer_seconds();

            print("Up ");
            print_padded(total / 3600, 2);
            print_char(':');
            print_padded((total / 60) % 60, 2);
            print_char(':');
            print_padded(total % 60, 2);
            kprintf(" (%d seconds)\n", (int)total);
            break;
        }
        case CMD_MEM: {
            const struct pmm_stats* memory = pmm_get_stats();
            uint32_t kernel_kb =
                (memory->kernel_end - memory->kernel_start) / 1024;

            kprintf("RAM: %d KB installed, %d KB usable\n",
                    (int)memory->total_kb, (int)memory->available_kb);
            kprintf("Kernel: 0x%x - 0x%x (%d KB)\n", memory->kernel_start,
                    memory->kernel_end, (int)kernel_kb);
            kprintf("Pages: %d total, %d free, %d used (%d KB each)\n",
                    (int)memory->total_pages, (int)memory->free_pages,
                    (int)memory->used_pages, PMM_PAGE_SIZE / 1024);
            kprintf("Boot memory map: %d regions\n", (int)memory->region_count);
            break;
        }
        case CMD_SLEEP: {
            if (count != 2) {
                print("Usage: sleep <seconds>\n");
                break;
            }

            uint32_t seconds = 0;
            int digits = parse_uint(words[1], &seconds);

            if (digits == 0 || words[1][digits] != '\0') {
                print("Error: sleep needs a whole number of seconds.\n");
                break;
            }

            if (seconds > MAX_SLEEP_SECONDS) {
                print("Error: you cannot sleep for more than a day.\n");
                break;
            }

            kprintf("Sleeping for %d seconds...\n", (int)seconds);
            timer_sleep(seconds * 1000);
            print("Done.\n");
            break;
        }
        default: {
            print("Error: command not found.\n");
            break;
        }
    }
}
