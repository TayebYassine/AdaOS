#include "command.h"

#include <stdint.h>

#include "adascript.h"
#include "ata.h"
#include "clock.h"
#include "diskfs.h"
#include "editor.h"
#include "fs.h"
#include "io.h"
#include "kmalloc.h"
#include "kprintf.h"
#include "libc.h"
#include "paging.h"
#include "pmm.h"
#include "scheduler.h"
#include "screen.h"
#include "shell.h"
#include "snake.h"
#include "tetris.h"
#include "timer.h"
#include "version.h"

// The shell's command dispatcher: splits the typed line into words,
// looks the first one up, and runs the matching command.

/* Maximum number of words in one command line. */
#define MAX_WORDS 32
/* Number of commands shown on one page of "help". */
#define COMMANDS_PER_PAGE 6
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
    CMD_TASKS,
    CMD_LS,
    CMD_CD,
    CMD_PWD,
    CMD_CAT,
    CMD_WRITE,
    CMD_MKDIR,
    CMD_RM,
    CMD_MV,
    CMD_CP,
    CMD_FIND,
    CMD_TREE,
    CMD_DISK,
    CMD_TIME,
    CMD_CAL,
    CMD_SETTIME,
    CMD_SETDATE,
    CMD_VER,
    CMD_SNAKE,
    CMD_TETRIS,
    CMD_RUN,
    CMD_NOT_FOUND = -1,
};

static const char* command_names[] = {
    "print",   "help",  "shutdown", "reboot", "clear", "uptime",
    "sleep",   "mem",   "tasks",    "ls",     "cd",    "pwd",
    "cat",     "write", "mkdir",    "rm",     "mv",    "cp",
    "find",    "tree",  "disk",     "time",   "cal",   "settime",
    "setdate", "ver",   "snake",    "tetris", "run"};

static const char* command_help[] = {
    "cal            - show this month's calendar",
    "cat <path>     - print a file",
    "cd <path>      - change the current directory",
    "clear          - clear the screen",
    "cp <src> <dst> - copy a file or directory",
    "disk           - show the disk and its file system",
    "find <name>    - find files whose name contains <name>",
    "help [page]    - show this list of commands",
    "ls [path]      - list a directory",
    "mem            - show RAM, paging, the heap and the kernel size",
    "mkdir <path>   - create a directory",
    "mv <src> <dst> - move or rename a file or directory",
    "print <text>   - print text on the screen",
    "pwd            - show the current directory",
    "reboot         - restart the computer",
    "rm <path>      - remove a file or directory",
    "run <path>     - run an AdaScript file",
    "setdate        - set the date",
    "settime        - set the time of day",
    "shutdown       - turn off the computer",
    "sleep <sec>    - pause for a number of seconds",
    "snake          - play the snake game",
    "tasks          - show the running tasks",
    "tetris         - play the tetris game",
    "time           - show the date and the time",
    "tree [path]    - show a directory as a tree",
    "uptime         - how long AdaOS has been running",
    "ver            - show the AdaOS version",
    "write <file>   - edit a file"};

/* Look up a command by name; returns its id or CMD_NOT_FOUND. */
static int find_command(const char* name) {
    for (int i = 0; i < (int)ARRAY_COUNT(command_names); i++) {
        if (strcmp(name, command_names[i]) == 0) {
            return i;
        }
    }

    return CMD_NOT_FOUND;
}

/* Split a line into words separated by spaces. A word may be wrapped in
 * double quotes to keep its spaces, as in  write "my file.txt" ; the
 * quotes themselves are dropped. Words are packed back into the line as
 * they are read, so "w" (the write position) never overtakes "i". */
static int split(char* line, char* words[], int max) {
    int count = 0;
    int i = 0;
    int w = 0;

    while (line[i] == ' ') {
        i++;
    }

    while (line[i] != '\0' && count < max) {
        int quoted = 0;

        words[count] = &line[w];
        count++;

        while (line[i] != '\0') {
            if (line[i] == '"') {
                quoted = !quoted;
                i++;
                continue;
            }
            if (line[i] == ' ' && !quoted) {
                i++;
                break;
            }

            line[w] = line[i];
            w++;
            i++;
        }

        line[w] = '\0';
        w++;

        while (line[i] == ' ') {
            i++;
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
 * number). Parsing stops after 10 digits, and stops early if the value would
 * not fit in a uint32_t. */
static int parse_uint(const char* word, uint32_t* value) {
    uint32_t result = 0;
    int digits = 0;

    while (digits < 10 && word[digits] >= '0' && word[digits] <= '9') {
        uint32_t digit = (uint32_t)(word[digits] - '0');

        if (result > (UINT32_MAX - digit) / 10u) {
            break;
        }

        result = result * 10 + digit;
        digits++;
    }

    *value = result;
    return digits;
}

/* Read exactly "count" digits at text[at] and turn them into a number;
 * returns 0 when they are not all digits. */
static int parse_digits(const char* text, int at, int count, int* value) {
    int result = 0;

    for (int i = 0; i < count; i++) {
        char c = text[at + i];

        if (c < '0' || c > '9') {
            return 0;
        }

        result = result * 10 + (c - '0');
    }

    *value = result;
    return 1;
}

/* Power off the machine through the ACPI "S5" (soft off) state. */
static void command_shutdown(void) {
    outw(0x604, 0x2000);   // QEMU / KVM
    outw(0xB004, 0x2000);  // Bochs / older QEMU
    outw(0x4004, 0x3400);  // VirtualBox

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

/* Look under "node" for names holding "pattern", printing the path of
 * each match; returns how many were found. */
static int find_walk(struct fs_node* node, const char* pattern) {
    char path[FS_PATH_MAX];
    int found = 0;

    if (strstr(node->name, pattern) != 0 && fs_node_path(node, path)) {
        kprintf("%s\n", path);
        found++;
    }

    for (struct fs_node* child = node->children; child != 0;
         child = child->sibling) {
        found += find_walk(child, pattern);
    }

    return found;
}

/* Room for the branches above the deepest entry the shell will draw:
 * four characters per level, deep enough for any path that fits. */
#define TREE_PREFIX_MAX 256

// The branch pieces, as the CP437 glyphs the VGA text font draws: the
// "├──" and "└──" in front of an entry, and the "│" that keeps the
// levels above it connected. Each is four cells wide.
#define TREE_TEE "\xC3\xC4\xC4 "
#define TREE_ELBOW "\xC0\xC4\xC4 "
#define TREE_BAR "\xB3   "
#define TREE_GAP "    "

/* Draw everything under "dir", indenting each level with the branches
 * built up in "prefix"; "len" is how much of it is in use. */
static void tree_walk(struct fs_node* dir, char* prefix, int len) {
    for (struct fs_node* node = dir->children; node != 0;
         node = node->sibling) {
        int last = (node->sibling == 0);
        int next = len;

        print(prefix);
        print(last ? TREE_ELBOW : TREE_TEE);
        print(node->name);
        if (node->kind == FS_DIR) {
            print_char('/');
        }
        print_char('\n');

        if (node->kind != FS_DIR || node->children == 0) {
            continue;
        }

        if (len + 4 < TREE_PREFIX_MAX) {
            memcpy(prefix + len, last ? TREE_GAP : TREE_BAR, 4);
            prefix[len + 4] = '\0';
            next = len + 4;
        }

        tree_walk(node, prefix, next);

        /* Drop the branch the children added, so the next sibling is
         * drawn at this level and not one level deeper. */
        prefix[len] = '\0';
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
            int total_commands = (int)ARRAY_COUNT(command_help);
            int total_pages =
                (total_commands + COMMANDS_PER_PAGE - 1) / COMMANDS_PER_PAGE;
            uint32_t page = 1;

            if (count > 1) {
                uint32_t requested = 0;
                int digits = parse_uint(words[1], &requested);

                if (count > 2 || digits == 0 || words[1][digits] != '\0' ||
                    requested < 1 || (int)requested > total_pages) {
                    print("Error: out of range\n");
                    break;
                }

                page = requested;
            }

            int start = ((int)page - 1) * COMMANDS_PER_PAGE;
            int end = start + COMMANDS_PER_PAGE;
            if (end > total_commands) {
                end = total_commands;
            }

            print("Available commands:\n");

            for (int i = start; i < end; i++) {
                print("  ");
                print(command_help[i]);
                print_char('\n');
            }

            if ((int)page == total_pages) {
                kprintf("\n  AdaOS version: %s\n", ADAOS_VERSION);
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
            clock_draw();
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

            const struct paging_stats* vm = paging_get_stats();
            const struct heap_stats* heap = kmalloc_get_stats();

            kprintf("Identity map: %d KB, %d page tables\n",
                    (int)(vm->identity_bytes / 1024), (int)vm->page_tables);
            kprintf("Heap: %d KB mapped, %d KB used, %d blocks\n",
                    (int)(heap->mapped_bytes / 1024),
                    (int)(heap->used_bytes / 1024), (int)heap->block_count);
            break;
        }
        case CMD_TASKS: {
            int count = scheduler_task_count();
            struct task_info info;

            kprintf("Tasks: %d\n", count);

            for (int i = 0; i < count; i++) {
                if (scheduler_task_info(i, &info) != 0) {
                    kprintf("  %s %s %d s\n", info.name, info.state,
                            (int)(info.runtime_ticks / 100));
                }
            }

            kprintf("Idle wakeups: %d\n", (int)scheduler_idle_ticks());
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

            scheduler_sleep_ms(seconds * 1000);

            break;
        }
        case CMD_LS: {
            const char* shown;
            struct fs_node* dir;

            if (count > 2) {
                print("Usage: ls [path]\n");
                break;
            }

            shown = (count == 2) ? words[1] : fs_cwd();
            dir = fs_resolve(shown);

            if (dir == 0) {
                break;
            }
            if (dir->kind != FS_DIR) {
                print("Error: that path is not a directory.\n");
                break;
            }

            kprintf("%s\n", shown);

            if (dir->children == 0) {
                print("(empty)\n");
                break;
            }

            for (struct fs_node* node = dir->children; node != 0;
                 node = node->sibling) {
                if (node->kind == FS_DIR) {
                    kprintf("  %s/\n", node->name);
                } else {
                    kprintf("  %s  (%d bytes)\n", node->name, (int)node->size);
                }
            }
            break;
        }
        case CMD_CD:
            if (count > 2) {
                print("Usage: cd <path>\n");
                break;
            }
            fs_chdir(count == 2 ? words[1] : "/");
            break;
        case CMD_PWD:
            kprintf("%s\n", fs_cwd());
            break;
        case CMD_CAT: {
            struct fs_node* node;

            if (count != 2) {
                print("Usage: cat <path>\n");
                break;
            }

            node = fs_resolve(words[1]);
            if (node == 0) {
                break;
            }
            if (node->kind == FS_DIR) {
                print("Error: that path is a directory.\n");
                break;
            }

            print(node->data);
            if (node->size == 0 || node->data[node->size - 1] != '\n') {
                print_char('\n');
            }
            break;
        }
        case CMD_WRITE:
            if (count != 2) {
                print("Usage: write <file>\n");
                break;
            }

            editor_open(words[1]);
            break;
        case CMD_MKDIR:
            if (count != 2) {
                print("Usage: mkdir <path>\n");
                break;
            }
            fs_mkdir(words[1]);
            break;
        case CMD_RM:
            if (count != 2) {
                print("Usage: rm <path>\n");
                break;
            }
            fs_remove(words[1]);
            break;
        case CMD_MV:
            if (count != 3) {
                print("Usage: mv <source> <destination>\n");
                break;
            }
            fs_move(words[1], words[2]);
            break;
        case CMD_CP:
            if (count != 3) {
                print("Usage: cp <source> <destination>\n");
                break;
            }
            fs_copy(words[1], words[2]);
            break;
        case CMD_FIND: {
            int found = 0;

            if (count != 2) {
                print("Usage: find <name>\n");
                break;
            }

            for (struct fs_node* node = fs_root()->children; node != 0;
                 node = node->sibling) {
                found += find_walk(node, words[1]);
            }

            if (found == 0) {
                print("No matches.\n");
            }
            break;
        }
        case CMD_TREE: {
            const char* shown;
            struct fs_node* dir;
            char prefix[TREE_PREFIX_MAX] = "";

            if (count > 2) {
                print("Usage: tree [path]\n");
                break;
            }

            shown = (count == 2) ? words[1] : fs_cwd();
            dir = fs_resolve(shown);

            if (dir == 0) {
                break;
            }
            if (dir->kind != FS_DIR) {
                print("Error: that path is not a directory.\n");
                break;
            }

            kprintf("%s\n", shown);
            tree_walk(dir, prefix, 0);
            break;
        }
        case CMD_DISK: {
            if (!diskfs_present()) {
                print("No disk on the primary IDE bus.\n");
                break;
            }

            kprintf("Disk: %s\n", ata_model());
            kprintf("Size: %d MB (%d sectors)\n",
                    (int)(diskfs_total_sectors() / 2048),
                    (int)diskfs_total_sectors());
            kprintf("Used: %d KB in %d of %d entries\n",
                    (int)(diskfs_used_sectors() / 2), (int)diskfs_entry_count(),
                    (int)diskfs_entry_capacity());
            break;
        }
        case CMD_TIME: {
            char stamp[24];

            clock_stamp(stamp, 1);
            kprintf("%s\n", stamp);
            break;
        }
        case CMD_CAL: {
            static const char* months[] = {"January", "February", "March",
                                           "April",   "May",      "June",
                                           "July",    "August",   "September",
                                           "October", "November", "December"};
            struct clock_time time;
            int title_len;
            int first;
            int days;

            clock_get(&time);

            title_len = (int)strlen(months[time.month - 1]) + 1;
            for (int y = time.year; y != 0; y /= 10) {
                title_len++;
            }
            for (int i = 0; i < (20 - title_len) / 2; i++) {
                print_char(' ');
            }
            kprintf("%s %d\n", months[time.month - 1], time.year);

            print("Su Mo Tu We Th Fr Sa\n");

            first = clock_day_of_week(time.year, time.month, 1) - 1;
            days = clock_days_in_month(time.year, time.month);

            for (int i = 0; i < first; i++) {
                print("   ");
            }

            for (int day = 1; day <= days; day++) {
                int column = (first + day - 1) % 7;

                if (day > 1) {
                    print_char((column == 0) ? '\n' : ' ');
                }
                if (day < 10) {
                    print_char(' ');
                }
                kprintf("%d", day);
            }

            print_char('\n');
            break;
        }
        case CMD_SETTIME: {
            char text[SHELL_LINE_MAX];
            int hour = 0;
            int minute = 0;
            int second = 0;
            int pm = 0;

            print("Please enter the time following the format HH:MM:SS: ");
            shell_read_line(text);

            if ((int)strlen(text) != 8 || text[2] != ':' || text[5] != ':' ||
                !parse_digits(text, 0, 2, &hour) ||
                !parse_digits(text, 3, 2, &minute) ||
                !parse_digits(text, 6, 2, &second) || hour < 1 || hour > 12 ||
                minute > 59 || second > 59) {
                print("Error: Incorrect format\n");
                break;
            }

            print("Is the time AM or PM? ");
            shell_read_line(text);

            if (strcmp(text, "AM") == 0 || strcmp(text, "am") == 0) {
                pm = 0;
            } else if (strcmp(text, "PM") == 0 || strcmp(text, "pm") == 0) {
                pm = 1;
            } else {
                print("Error: the time must be AM or PM.\n");
                break;
            }

            if (hour == 12) {
                hour = 0;
            }
            if (pm) {
                hour += 12;
            }

            clock_set_time(hour, minute, second);
            print("Time set.\n");
            break;
        }
        case CMD_SETDATE: {
            char text[SHELL_LINE_MAX];
            int month = 0;
            int day = 0;
            int year = 0;

            print("Please enter the date following the format MM-DD-YYYY: ");
            shell_read_line(text);

            if ((int)strlen(text) != 10 || text[2] != '-' || text[5] != '-' ||
                !parse_digits(text, 0, 2, &month) ||
                !parse_digits(text, 3, 2, &day) ||
                !parse_digits(text, 6, 4, &year) || year < 1 || month < 1 ||
                month > 12 || day < 1 ||
                day > clock_days_in_month(year, month)) {
                print("Error: Incorrect format\n");
                break;
            }

            clock_set_date(year, month, day);
            print("Date set.\n");
            break;
        }
        case CMD_VER:
            kprintf("%s\n", ADAOS_VERSION);
            break;
        case CMD_SNAKE:
            snake_run();
            break;
        case CMD_TETRIS:
            tetris_run();
            break;
        case CMD_RUN: {
            if (count != 2) {
                print("Usage: run <path>\n");
                break;
            }
            adascript_run_file(words[1]);
            break;
        }
        default: {
            int len = (int)strlen(words[0]);
            if (len > 3 && strcmp(words[0] + len - 3, ".as") == 0) {
                adascript_run_file(words[0]);
                break;
            }
            print("Error: command not found.\n");
            break;
        }
    }

    diskfs_sync();
}
